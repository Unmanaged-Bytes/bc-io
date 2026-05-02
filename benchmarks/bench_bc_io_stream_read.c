// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_mmap.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void create_test_file(const char* path, size_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char buf[65536];
    bc_core_fill(buf, sizeof(buf), 'X');
    size_t written = 0;
    while (written < size) {
        size_t chunk = size - written;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        (void)write(fd, buf, chunk);
        written += chunk;
    }
    close(fd);
}

static void bench_stream_file(const char* path, size_t buffer_size)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, buffer_size, &stream);

    size_t total_bytes = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        total_bytes += chunk.size;
    }
    uint64_t elapsed = now_ns() - t0;

    double gbps = (double)total_bytes / (double)elapsed;
    printf("  file buf=%-5zu  %5.1f ms  %5.1f GB/s  %zu bytes\n", buffer_size, (double)elapsed / 1e6, gbps, total_bytes);

    bc_io_stream_close(stream);
    bc_allocators_context_destroy(ctx);
}

static void bench_stream_mmap(const char* path)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_mmap_options_t options = {
        .read_only = true,
        .populate = false,
        .hugepages_hint = false,
        .madvise_hint = BC_IO_MADVISE_NORMAL,
        .offset = 0,
        .length = 0,
    };
    bc_io_mmap_t* map = NULL;
    bc_io_mmap_file(ctx, path, &options, &map);

    bc_io_stream_t* stream = NULL;
    bc_io_mmap_get_stream(map, &stream);

    size_t total_bytes = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        total_bytes += chunk.size;
    }
    uint64_t elapsed = now_ns() - t0;

    double gbps = (double)total_bytes / (double)elapsed;
    printf("  mmap           %5.1f ms  %5.1f GB/s  %zu bytes\n", (double)elapsed / 1e6, gbps, total_bytes);

    bc_io_mmap_destroy(map);
    bc_allocators_context_destroy(ctx);
}

static void bench_stream_advise_drop(const char* path, size_t buffer_size)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, buffer_size, &stream);
    size_t total_bytes = 0;
    uint64_t t0 = now_ns();
    for (;;) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        total_bytes += chunk.size;
    }
    uint64_t elapsed_first = now_ns() - t0;
    bc_io_stream_advise(stream, BC_IO_STREAM_ACCESS_DROP);
    bc_io_stream_close(stream);

    stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, buffer_size, &stream);
    total_bytes = 0;
    t0 = now_ns();
    for (;;) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        total_bytes += chunk.size;
    }
    uint64_t elapsed_second = now_ns() - t0;
    bc_io_stream_close(stream);

    double gbps_first = (double)total_bytes / (double)elapsed_first;
    double gbps_second = (double)total_bytes / (double)elapsed_second;
    printf("  advise_drop buf=%-5zu  first: %5.1f GB/s  second: %5.1f GB/s  %zu bytes\n", buffer_size, gbps_first, gbps_second,
           total_bytes);

    bc_allocators_context_destroy(ctx);
}

/* ===== Memory stream vs bc_core_copy direct ===== */

static void bench_stream_memory_vs_copy(const char* label, size_t size, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    void* src = NULL;
    void* dst = NULL;
    bc_allocators_pool_allocate(ctx, size, &src);
    bc_allocators_pool_allocate(ctx, size, &dst);
    bc_core_fill(src, size, (unsigned char)0xAB);

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        bc_io_stream_t* stream = NULL;
        bc_io_stream_open_memory(ctx, src, size, &stream);
        size_t offset = 0;
        for (;;) {
            bc_io_stream_chunk_t chunk = {0};
            if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
                break;
            }
            bc_core_copy((unsigned char*)dst + offset, chunk.data, chunk.size);
            offset += chunk.size;
        }
        bc_io_stream_close(stream);
    }
    uint64_t stream_ns = now_ns() - t0;

    t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        bc_core_copy(dst, src, size);
    }
    uint64_t copy_ns = now_ns() - t0;

    double stream_gbps = (double)size * (double)iters / (double)stream_ns;
    double copy_gbps = (double)size * (double)iters / (double)copy_ns;
    double overhead_pct = stream_ns > copy_ns ? 100.0 * (double)(stream_ns - copy_ns) / (double)copy_ns : 0.0;

    printf("  memory stream %-6s  %5.2f GB/s  direct: %5.2f GB/s  overhead: %.0f%%\n", label, stream_gbps, copy_gbps, overhead_pct);

    bc_allocators_pool_free(ctx, dst);
    bc_allocators_pool_free(ctx, src);
    bc_allocators_context_destroy(ctx);
}

int main(void)
{
    const char* path = "/var/benchmarks/bc_bench_stream.dat";
    size_t file_size = 64 * 1024 * 1024;

    printf("bench_stream_read (%zu MB file)\n\n", file_size / (1024 * 1024));

    create_test_file(path, file_size);

    bench_stream_file(path, 4096);
    bench_stream_file(path, 65536);
    bench_stream_file(path, 262144);
    bench_stream_mmap(path);

    printf("\n--- advise drop ---\n");
    bench_stream_advise_drop(path, 65536);

    unlink(path);
    printf("\n");

    printf("--- memory stream vs direct copy ---\n");
    bench_stream_memory_vs_copy("4KB", 4096, 1000000);
    bench_stream_memory_vs_copy("64KB", 65536, 100000);
    bench_stream_memory_vs_copy("1MB", 1048576, 10000);

    return 0;
}
