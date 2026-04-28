// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_concurrency.h"
#include "bc_io_random.h"
#include "bc_io_stream.h"
#include "bc_io_walk.h"

#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <ftw.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIXTURE_DIR_COUNT 100
#define FIXTURE_FILE_PER_DIR 100
#define FIXTURE_TOTAL_FILES (FIXTURE_DIR_COUNT * FIXTURE_FILE_PER_DIR)

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void use_ptr(const void* p)
{
    __asm__ volatile("" : : "r"(p) : "memory");
}

static int build_fixture(const char* root)
{
    if (mkdir(root, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    for (int d = 0; d < FIXTURE_DIR_COUNT; d++) {
        char dirpath[512];
        snprintf(dirpath, sizeof(dirpath), "%s/dir%03d", root, d);
        if (mkdir(dirpath, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        for (int f = 0; f < FIXTURE_FILE_PER_DIR; f++) {
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/file%03d", dirpath, f);
            int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) {
                return -1;
            }
            if (write(fd, "x", 1) != 1) {
                close(fd);
                return -1;
            }
            close(fd);
        }
    }
    return 0;
}

static int rm_walker(const char* path, const struct stat* sb, int type, struct FTW* ftw)
{
    (void)sb;
    (void)type;
    (void)ftw;
    return remove(path);
}

static void cleanup_fixture(const char* root)
{
    nftw(root, rm_walker, 16, FTW_DEPTH | FTW_PHYS);
}

static void create_test_file(const char* path, size_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }
    char buf[65536];
    memset(buf, 'X', sizeof(buf));
    size_t written = 0;
    while (written < size) {
        size_t chunk = size - written;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        ssize_t n = write(fd, buf, chunk);
        if (n < 0) {
            break;
        }
        written += (size_t)n;
    }
    close(fd);
}

/* ===== Walk: bc_io_walk_parallel vs nftw vs fts ===== */

static atomic_size_t walk_visit_counter;

static bool bc_walk_visit(const bc_io_walk_entry_t* entry, void* user_data)
{
    (void)user_data;
    if (entry->kind == BC_IO_WALK_ENTRY_FILE) {
        atomic_fetch_add_explicit(&walk_visit_counter, 1, memory_order_relaxed);
        use_ptr(entry->absolute_path);
    }
    return true;
}

static size_t nftw_count;

static int nftw_visit(const char* path, const struct stat* sb, int type, struct FTW* ftw)
{
    (void)sb;
    (void)ftw;
    if (type == FTW_F) {
        nftw_count++;
        use_ptr(path);
    }
    return 0;
}

static size_t bench_walk_bc_io(const char* root)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_concurrency_context_t* conc = NULL;
    bc_concurrency_config_t concurrency_config = {.worker_count = 4};
    bc_concurrency_create(ctx, &concurrency_config, &conc);

    atomic_store_explicit(&walk_visit_counter, 0, memory_order_relaxed);

    bc_io_walk_config_t config = {
        .root = root,
        .root_length = strlen(root),
        .main_memory_context = ctx,
        .concurrency_context = conc,
        .visit = bc_walk_visit,
    };
    bc_io_walk_stats_t stats = {0};

    uint64_t t0 = now_ns();
    bc_io_walk_parallel(&config, &stats);
    uint64_t elapsed = now_ns() - t0;

    size_t visited = atomic_load_explicit(&walk_visit_counter, memory_order_relaxed);
    double files_per_sec = (double)visited / ((double)elapsed / 1e9);
    printf("  bc_io_walk_parallel  files=%5zu  %6.2f ms  %7.0f files/sec\n", visited, (double)elapsed / 1e6, files_per_sec);

    bc_concurrency_destroy(conc);
    bc_allocators_context_destroy(ctx);
    return (size_t)elapsed;
}

static size_t bench_walk_nftw(const char* root)
{
    nftw_count = 0;
    uint64_t t0 = now_ns();
    nftw(root, nftw_visit, 16, FTW_PHYS);
    uint64_t elapsed = now_ns() - t0;

    double files_per_sec = (double)nftw_count / ((double)elapsed / 1e9);
    printf("  nftw(FTW_PHYS)       files=%5zu  %6.2f ms  %7.0f files/sec\n", nftw_count, (double)elapsed / 1e6, files_per_sec);
    return (size_t)elapsed;
}

static size_t bench_walk_fts(const char* root)
{
    char* paths[2] = {(char*)root, NULL};
    size_t count = 0;
    uint64_t t0 = now_ns();
    FTS* fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (fts != NULL) {
        const FTSENT* entry;
        while ((entry = fts_read(fts)) != NULL) {
            if (entry->fts_info == FTS_F) {
                count++;
                use_ptr(entry->fts_path);
            }
        }
        fts_close(fts);
    }
    uint64_t elapsed = now_ns() - t0;

    double files_per_sec = (double)count / ((double)elapsed / 1e9);
    printf("  fts_open/fts_read    files=%5zu  %6.2f ms  %7.0f files/sec\n", count, (double)elapsed / 1e6, files_per_sec);
    return (size_t)elapsed;
}

/* ===== Stream sequential read: bc_io_stream vs fread vs read+buffer ===== */

static size_t bench_stream_bc_io(const char* path, size_t buffer_size)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, buffer_size, &stream);

    size_t total = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        total += chunk.size;
        use_ptr(chunk.data);
    }
    uint64_t elapsed = now_ns() - t0;

    bc_io_stream_close(stream);
    bc_allocators_context_destroy(ctx);

    double mb_per_s = (double)total / 1048576.0 / ((double)elapsed / 1e9);
    printf("  bc_io_stream  buf=%-7zu  %5.2f ms  %7.0f MB/s\n", buffer_size, (double)elapsed / 1e6, mb_per_s);
    return (size_t)elapsed;
}

static size_t bench_stream_fread(const char* path, size_t buffer_size)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    setvbuf(fp, NULL, _IOFBF, buffer_size);
    void* buf = malloc(buffer_size);
    if (buf == NULL) {
        fclose(fp);
        return 0;
    }

    size_t total = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        size_t n = fread(buf, 1, buffer_size, fp);
        if (n == 0) {
            break;
        }
        total += n;
        use_ptr(buf);
    }
    uint64_t elapsed = now_ns() - t0;

    free(buf);
    fclose(fp);

    double mb_per_s = (double)total / 1048576.0 / ((double)elapsed / 1e9);
    printf("  fread         buf=%-7zu  %5.2f ms  %7.0f MB/s\n", buffer_size, (double)elapsed / 1e6, mb_per_s);
    return (size_t)elapsed;
}

static size_t bench_stream_read_raw(const char* path, size_t buffer_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    void* buf = malloc(buffer_size);
    if (buf == NULL) {
        close(fd);
        return 0;
    }

    size_t total = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        ssize_t n = read(fd, buf, buffer_size);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
        use_ptr(buf);
    }
    uint64_t elapsed = now_ns() - t0;

    free(buf);
    close(fd);

    double mb_per_s = (double)total / 1048576.0 / ((double)elapsed / 1e9);
    printf("  read(raw)     buf=%-7zu  %5.2f ms  %7.0f MB/s\n", buffer_size, (double)elapsed / 1e6, mb_per_s);
    return (size_t)elapsed;
}

/* ===== File open/close: bc_io_stream_open_file vs open/close raw ===== */

static double bench_open_close_bc_io(const char* path, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    {
        bc_io_stream_t* warm = NULL;
        bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, 0, &warm);
        bc_io_stream_close(warm);
    }

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        bc_io_stream_t* stream = NULL;
        bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, 0, &stream);
        bc_io_stream_close(stream);
    }
    uint64_t elapsed = now_ns() - t0;

    bc_allocators_context_destroy(ctx);

    double ns_op = (double)elapsed / (double)iters;
    printf("  bc_io_stream_open_file+close  %6.0f ns/op\n", ns_op);
    return ns_op;
}

static double bench_open_close_libc(const char* path, size_t iters)
{
    {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            close(fd);
        }
    }

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        int fd = open(path, O_RDONLY);
        use_ptr(&fd);
        close(fd);
    }
    uint64_t elapsed = now_ns() - t0;

    double ns_op = (double)elapsed / (double)iters;
    printf("  open(2)+close(2)              %6.0f ns/op\n", ns_op);
    return ns_op;
}

/* ===== Random bytes: bc_io_random_bytes vs getrandom direct ===== */

static double bench_random_bc_io(size_t total_bytes, size_t chunk_size)
{
    void* buf = malloc(chunk_size);
    if (buf == NULL) {
        return 0.0;
    }
    size_t produced = 0;
    uint64_t t0 = now_ns();
    while (produced < total_bytes) {
        bc_io_random_bytes(buf, chunk_size);
        produced += chunk_size;
        use_ptr(buf);
    }
    uint64_t elapsed = now_ns() - t0;
    free(buf);

    double mb_per_s = (double)produced / 1048576.0 / ((double)elapsed / 1e9);
    printf("  bc_io_random_bytes  chunk=%-5zu  total=%-6zu  %5.2f ms  %7.1f MB/s\n", chunk_size, produced, (double)elapsed / 1e6, mb_per_s);
    return mb_per_s;
}

static double bench_random_getrandom(size_t total_bytes, size_t chunk_size)
{
    void* buf = malloc(chunk_size);
    if (buf == NULL) {
        return 0.0;
    }
    size_t produced = 0;
    uint64_t t0 = now_ns();
    while (produced < total_bytes) {
        ssize_t n = getrandom(buf, chunk_size, 0);
        if (n < 0) {
            break;
        }
        produced += (size_t)n;
        use_ptr(buf);
    }
    uint64_t elapsed = now_ns() - t0;
    free(buf);

    double mb_per_s = (double)produced / 1048576.0 / ((double)elapsed / 1e9);
    printf("  getrandom(2)        chunk=%-5zu  total=%-6zu  %5.2f ms  %7.1f MB/s\n", chunk_size, produced, (double)elapsed / 1e6, mb_per_s);
    return mb_per_s;
}

int main(void)
{
    printf("bench_bc_io_vs_libc\n\n");

    printf("--- walk 10000 files (100 dirs x 100 files) ---\n");
    char fixture[256];
    snprintf(fixture, sizeof(fixture), "/tmp/bc-io-bench-fixture-%d", getpid());
    if (build_fixture(fixture) != 0) {
        fprintf(stderr, "fixture build failed\n");
        cleanup_fixture(fixture);
        return 1;
    }

    size_t bc_walk_ns = bench_walk_bc_io(fixture);
    size_t nftw_ns = bench_walk_nftw(fixture);
    size_t fts_ns = bench_walk_fts(fixture);
    if (nftw_ns > 0 && fts_ns > 0 && bc_walk_ns > 0) {
        printf("  ratios: bc/nftw=%.2fx  bc/fts=%.2fx  (lower=bc faster)\n", (double)bc_walk_ns / (double)nftw_ns,
               (double)bc_walk_ns / (double)fts_ns);
    }
    cleanup_fixture(fixture);

    printf("\n--- stream sequential read 64 MB file ---\n");
    const char* stream_path = "/tmp/bc-io-bench-stream.dat";
    size_t file_size = 64 * 1024 * 1024;
    create_test_file(stream_path, file_size);

    static const size_t buffer_sizes[] = {4096, 65536, 1048576};
    static const size_t num_buffers = sizeof(buffer_sizes) / sizeof(buffer_sizes[0]);
    for (size_t i = 0; i < num_buffers; i++) {
        bench_stream_bc_io(stream_path, buffer_sizes[i]);
        bench_stream_fread(stream_path, buffer_sizes[i]);
        bench_stream_read_raw(stream_path, buffer_sizes[i]);
        printf("\n");
    }

    printf("--- file open/close (warm cache, 100000 iters) ---\n");
    double bc_open_ns = bench_open_close_bc_io(stream_path, 100000);
    double libc_open_ns = bench_open_close_libc(stream_path, 100000);
    if (libc_open_ns > 0.0) {
        printf("  ratio: bc/libc=%.2fx  (lower=bc faster, gap=allocator overhead)\n", bc_open_ns / libc_open_ns);
    }

    unlink(stream_path);

    printf("\n--- random bytes 1 MB total (4 KB chunks) ---\n");
    double bc_rand_mbps = bench_random_bc_io(1048576, 4096);
    double getrandom_mbps = bench_random_getrandom(1048576, 4096);
    if (getrandom_mbps > 0.0) {
        printf("  ratio: bc/getrandom=%.2fx  (higher=bc faster)\n", bc_rand_mbps / getrandom_mbps);
    }

    return 0;
}
