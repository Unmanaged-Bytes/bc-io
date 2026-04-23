// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "bc_io_stream.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bc_allocators_context_t* test_memory = NULL;

static int setup(void** state)
{
    (void)state;
    return bc_allocators_context_create(NULL, &test_memory) ? 0 : -1;
}

static int teardown(void** state)
{
    (void)state;
    bc_allocators_context_destroy(test_memory);
    test_memory = NULL;
    return 0;
}

static void test_file_read_chunk_buffer_is_64_byte_aligned(void** state)
{
    (void)state;
    char path[] = "/tmp/bc_io_stream_align_XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);

    const size_t data_size = 4096;
    void* write_data = NULL;
    bc_allocators_pool_allocate(test_memory, data_size, &write_data);
    bc_core_fill(write_data, data_size, (unsigned char)0xAB);
    (void)write(fd, write_data, data_size);
    close(fd);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file(test_memory, path, BC_IO_STREAM_MODE_READ, 0, &stream));

    bc_io_stream_chunk_t chunk = {0};
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_non_null(chunk.data);
    assert_int_equal((uintptr_t)chunk.data % 64, 0);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(test_memory, write_data);
    unlink(path);
}

static void test_file_read_non_multiple_buffer_size_is_still_aligned(void** state)
{
    (void)state;
    char path[] = "/tmp/bc_io_stream_align_XXXXXX";
    int fd = mkstemp(path);
    assert_true(fd >= 0);

    const size_t data_size = 4096;
    void* write_data = NULL;
    bc_allocators_pool_allocate(test_memory, data_size, &write_data);
    bc_core_fill(write_data, data_size, (unsigned char)0xCD);
    (void)write(fd, write_data, data_size);
    close(fd);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file(test_memory, path, BC_IO_STREAM_MODE_READ, 100, &stream));

    bc_io_stream_chunk_t chunk = {0};
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_non_null(chunk.data);
    assert_int_equal((uintptr_t)chunk.data % 64, 0);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(test_memory, write_data);
    unlink(path);
}

static void test_pipe_read_chunk_buffer_is_64_byte_aligned(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    const size_t data_size = 4096;
    void* write_data = NULL;
    bc_allocators_pool_allocate(test_memory, data_size, &write_data);
    bc_core_fill(write_data, data_size, (unsigned char)0xEF);
    (void)write(fds[1], write_data, data_size);
    close(fds[1]);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ, 0, &stream));

    bc_io_stream_chunk_t chunk = {0};
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_non_null(chunk.data);
    assert_int_equal((uintptr_t)chunk.data % 64, 0);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(test_memory, write_data);
    close(fds[0]);
}

static void test_memory_stream_chunk_points_into_source_data(void** state)
{
    (void)state;
    const size_t data_size = 256;
    void* source = NULL;
    bc_allocators_pool_allocate(test_memory, data_size, &source);
    bc_core_fill(source, data_size, (unsigned char)0x77);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_memory(test_memory, source, data_size, &stream));

    bc_io_stream_chunk_t chunk = {0};
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_non_null(chunk.data);
    assert_false(chunk.owned);

    const unsigned char* source_bytes = (const unsigned char*)source;
    const unsigned char* chunk_bytes = (const unsigned char*)chunk.data;
    assert_true(chunk_bytes >= source_bytes);
    assert_true(chunk_bytes < source_bytes + data_size);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(test_memory, source);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_file_read_chunk_buffer_is_64_byte_aligned, setup, teardown),
        cmocka_unit_test_setup_teardown(test_file_read_non_multiple_buffer_size_is_still_aligned, setup, teardown),
        cmocka_unit_test_setup_teardown(test_pipe_read_chunk_buffer_is_64_byte_aligned, setup, teardown),
        cmocka_unit_test_setup_teardown(test_memory_stream_chunk_points_into_source_data, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
