// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "bc_io_stream.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"

#include <stdbool.h>
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

static void test_write_pipe_basic(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 0, &stream));
    assert_non_null(stream);

    const char content[] = "hello\n";
    const size_t content_size = sizeof(content) - 1;
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(stream, content, content_size, &written));
    assert_int_equal(written, content_size);
    assert_true(bc_io_stream_flush(stream));
    bc_io_stream_close(stream);

    char received[16] = {0};
    ssize_t received_bytes = read(fds[0], received, sizeof(received));
    assert_int_equal((size_t)received_bytes, content_size);
    assert_memory_equal(received, content, content_size);

    close(fds[0]);
    close(fds[1]);
}

static void test_write_pipe_source_type_is_pipe(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 0, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_PIPE);

    bc_io_stream_close(stream);
    close(fds[0]);
    close(fds[1]);
}

static void test_write_pipe_buffered_multi_write_single_flush(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 65536, &stream));

    const char part_a[] = "foo";
    const char part_b[] = "bar";
    const char part_c[] = "baz";
    size_t written = 0;

    assert_true(bc_io_stream_write_chunk(stream, part_a, sizeof(part_a) - 1, &written));
    assert_int_equal(written, sizeof(part_a) - 1);
    assert_true(bc_io_stream_write_chunk(stream, part_b, sizeof(part_b) - 1, &written));
    assert_int_equal(written, sizeof(part_b) - 1);
    assert_true(bc_io_stream_write_chunk(stream, part_c, sizeof(part_c) - 1, &written));
    assert_int_equal(written, sizeof(part_c) - 1);

    assert_true(bc_io_stream_flush(stream));
    bc_io_stream_close(stream);

    char received[16] = {0};
    ssize_t received_bytes = read(fds[0], received, sizeof(received));
    assert_int_equal(received_bytes, 9);
    assert_memory_equal(received, "foobarbaz", 9);

    close(fds[0]);
    close(fds[1]);
}

static void test_write_pipe_stats_bytes_written(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 0, &stream));

    void* data = NULL;
    bc_allocators_pool_allocate(test_memory, 512, &data);
    bc_core_fill(data, 512, (unsigned char)0xAA);

    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(stream, data, 512, &written));
    assert_int_equal(written, 512);
    assert_true(bc_io_stream_flush(stream));

    bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(stream, &stats));
    assert_int_equal(stats.bytes_written, 512);
    assert_int_equal(stats.write_count, 1);

    bc_io_stream_close(stream);
    bc_allocators_pool_free(test_memory, data);
    close(fds[0]);
    close(fds[1]);
}

static void test_write_pipe_close_flushes_pending_data(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(test_memory, fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE, 65536, &stream));

    const char content[] = "pending";
    const size_t content_size = sizeof(content) - 1;
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(stream, content, content_size, &written));
    assert_int_equal(written, content_size);

    bc_io_stream_close(stream);

    char received[16] = {0};
    ssize_t received_bytes = read(fds[0], received, sizeof(received));
    assert_int_equal((size_t)received_bytes, content_size);
    assert_memory_equal(received, content, content_size);

    close(fds[0]);
    close(fds[1]);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_write_pipe_basic, setup, teardown),
        cmocka_unit_test_setup_teardown(test_write_pipe_source_type_is_pipe, setup, teardown),
        cmocka_unit_test_setup_teardown(test_write_pipe_buffered_multi_write_single_flush, setup, teardown),
        cmocka_unit_test_setup_teardown(test_write_pipe_stats_bytes_written, setup, teardown),
        cmocka_unit_test_setup_teardown(test_write_pipe_close_flushes_pending_data, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
