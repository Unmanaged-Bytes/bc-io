// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
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

/* ===== Write file throughput ===== */

static void bench_stream_write_file(const char* path, size_t buffer_size, size_t file_size)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    void* chunk_data = NULL;
    bc_allocators_pool_allocate(ctx, buffer_size, &chunk_data);
    bc_core_fill(chunk_data, buffer_size, (unsigned char)0xAB);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_WRITE, buffer_size, &stream);

    uint64_t t0 = now_ns();
    size_t total_written = 0;
    size_t remaining = file_size;
    while (remaining > 0) {
        size_t chunk_size = remaining < buffer_size ? remaining : buffer_size;
        size_t written = 0;
        bc_io_stream_write_chunk(stream, chunk_data, chunk_size, &written);
        total_written += written;
        remaining -= chunk_size;
    }
    bc_io_stream_close(stream);
    uint64_t elapsed = now_ns() - t0;

    double gb_per_s = (double)total_written / (double)elapsed;
    printf("  write file buf=%-6zu  %5.2f GB/s\n", buffer_size, gb_per_s);

    bc_allocators_pool_free(ctx, chunk_data);
    bc_allocators_context_destroy(ctx);
    unlink(path);
}

/* ===== Flush overhead ===== */

static void bench_stream_flush_overhead(const char* path, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    void* chunk_data = NULL;
    bc_allocators_pool_allocate(ctx, 4096, &chunk_data);
    bc_core_fill(chunk_data, 4096, (unsigned char)0x55);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_WRITE, 65536, &stream);

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        size_t written = 0;
        bc_io_stream_write_chunk(stream, chunk_data, 4096, &written);
        bc_io_stream_flush(stream);
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  flush overhead 4KB    %6.0f ns/flush\n", (double)elapsed / (double)iters);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(ctx, chunk_data);
    bc_allocators_context_destroy(ctx);
    unlink(path);
}

/* ===== Write pipe latency ===== */

static void bench_stream_write_pipe(size_t batch_size, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    void* chunk_data = NULL;
    bc_allocators_pool_allocate(ctx, 4096, &chunk_data);
    bc_core_fill(chunk_data, 4096, (unsigned char)0xCC);

    void* drain_buf = NULL;
    bc_allocators_pool_allocate(ctx, batch_size * 4096, &drain_buf);

    int fds[2];
    pipe(fds);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    bc_io_stream_t* stream = NULL;
    bc_io_stream_open_file_descriptor(ctx, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 4096, &stream);

    uint64_t total_write_ns = 0;
    for (size_t i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        for (size_t j = 0; j < batch_size; j++) {
            size_t written = 0;
            bc_io_stream_write_chunk(stream, chunk_data, 4096, &written);
            bc_io_stream_flush(stream);
        }
        total_write_ns += now_ns() - t0;
        (void)read(fds[0], drain_buf, batch_size * 4096);
    }

    size_t total_ops = iters * batch_size;
    printf("  write pipe %2zu×4KB+flush  %5.0f ns/op    %.0f Kops/s\n", batch_size, (double)total_write_ns / (double)total_ops,
           (double)total_ops / ((double)total_write_ns / 1e9) / 1e3);

    bc_io_stream_close(stream);
    close(fds[0]);
    bc_allocators_pool_free(ctx, drain_buf);
    bc_allocators_pool_free(ctx, chunk_data);
    bc_allocators_context_destroy(ctx);
}

/* ===== Stream open+close: bc-allocators arena + pool allocation cost ===== */

static void bench_stream_open_close(const char* path, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    {
        bc_io_stream_t* init = NULL;
        bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_WRITE, 0, &init);
        bc_io_stream_close(init);
    }

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        bc_io_stream_t* stream = NULL;
        bc_io_stream_open_file(ctx, path, BC_IO_STREAM_MODE_READ, 0, &stream);
        bc_io_stream_close(stream);
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  open+close (arena+pool) %5.0f ns/op\n", (double)elapsed / (double)iters);

    bc_allocators_context_destroy(ctx);
    unlink(path);
}

/* ===== Multi-stream allocation: bc-allocators pressure with N concurrent arenas ===== */

static void bench_stream_multi_open(size_t num_streams, size_t iters)
{
    bc_allocators_context_t* ctx = NULL;
    bc_allocators_context_create(NULL, &ctx);

    bc_io_stream_t** streams = NULL;
    bc_allocators_pool_allocate(ctx, num_streams * sizeof(bc_io_stream_t*), (void**)&streams);

    int fds[2];
    pipe(fds);

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < iters; i++) {
        for (size_t j = 0; j < num_streams; j++) {
            bc_io_stream_open_file_descriptor(ctx, fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ, 0, &streams[j]);
        }
        for (size_t j = 0; j < num_streams; j++) {
            bc_io_stream_close(streams[j]);
        }
    }
    uint64_t elapsed = now_ns() - t0;

    printf("  multi-stream %2zu open+close  %5.0f ns/stream\n", num_streams, (double)elapsed / (double)(iters * num_streams));

    close(fds[0]);
    close(fds[1]);
    bc_allocators_pool_free(ctx, streams);
    bc_allocators_context_destroy(ctx);
}

int main(void)
{
    const char* write_path = "/tmp/bc_bench_stream_write.dat";
    const size_t file_size = 64 * 1024 * 1024;

    printf("bench_stream_write\n\n");

    printf("--- write file throughput ---\n");
    bench_stream_write_file(write_path, 4096, file_size);
    bench_stream_write_file(write_path, 65536, file_size);
    bench_stream_write_file(write_path, 262144, file_size);
    printf("\n");

    printf("--- flush overhead ---\n");
    bench_stream_flush_overhead(write_path, 100000);
    printf("\n");

    printf("--- pipe write latency ---\n");
    bench_stream_write_pipe(8, 10000);
    bench_stream_write_pipe(16, 5000);
    printf("\n");

    printf("--- bc-allocators allocation pressure ---\n");
    bench_stream_open_close(write_path, 100000);
    bench_stream_multi_open(8, 10000);
    bench_stream_multi_open(64, 1000);

    return 0;
}
