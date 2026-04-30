// SPDX-License-Identifier: MIT

#include "bc_io_file.h"
#include "bc_io_file_inode.h"
#include "bc_io_mmap.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_io_stream.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void bench_inode_set_insert(size_t count)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_file_inode_set_t* set = NULL;
    bc_io_file_inode_set_create(ctx, count / 2, &set);

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < count; i++) {
        bool was_present = false;
        bc_io_file_inode_set_insert(set, (dev_t)1, (ino_t)(i + 1), &was_present);
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  inode_set insert (%zu)  %5.0f ns/op  %.1f Mops/s\n", count, (double)elapsed / (double)count,
           (double)count / ((double)elapsed / 1e9) / 1e6);

    bc_io_file_inode_set_destroy(set);
    bc_allocators_context_destroy(ctx);
}

static void bench_inode_set_contains(size_t count)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_file_inode_set_t* set = NULL;
    bc_io_file_inode_set_create(ctx, count / 2, &set);

    for (size_t i = 0; i < count; i++) {
        bool was_present = false;
        bc_io_file_inode_set_insert(set, (dev_t)1, (ino_t)(i + 1), &was_present);
    }

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < count; i++) {
        bool found = false;
        bc_io_file_inode_set_contains(set, (dev_t)1, (ino_t)(i + 1), &found);
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  inode_set contains (%zu)  %5.0f ns/op  %.1f Mops/s\n", count, (double)elapsed / (double)count,
           (double)count / ((double)elapsed / 1e9) / 1e6);

    bc_io_file_inode_set_destroy(set);
    bc_allocators_context_destroy(ctx);
}

static void bench_map_file(void)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    const char* path = "/tmp/bc_bench_map.dat";
    size_t file_size = 16 * 1024 * 1024;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)file_size) != 0) {
            close(fd);
            return;
        }
        close(fd);
    }

    size_t iters = 1000;
    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        bc_io_mmap_t* map = NULL;
        bc_io_mmap_options_t opts = {.populate = false};
        bc_io_mmap_file(ctx, path, &opts, &map);
        bc_io_mmap_destroy(map);
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  map_file 16MB          %5.0f ns/cycle  %.0f Kcycles/s\n", (double)elapsed / (double)iters,
           (double)iters / ((double)elapsed / 1e9) / 1e3);

    unlink(path);
    bc_allocators_context_destroy(ctx);
}

int main(void)
{
    printf("bench_filesystem_ops\n\n");

    bench_inode_set_insert(100000);
    bench_inode_set_insert(1000000);
    bench_inode_set_contains(1000000);
    printf("\n");
    bench_map_file();

    return 0;
}
