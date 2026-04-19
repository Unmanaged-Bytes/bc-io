// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#define TEST_BUFFER_SIZE 256

struct test_fixture {
    bc_allocators_context_t* memory_context;
};

static int group_setup(void** state)
{
    struct test_fixture* fixture = test_calloc(1, sizeof(struct test_fixture));

    if (!bc_allocators_context_create(NULL, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }

    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct test_fixture* fixture = *state;
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static void test_read_pipe_basic(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xAB);

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    ssize_t written = write(pipe_fds[1], data, 100);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);
    assert_true(chunk.owned);
    assert_memory_equal(chunk.data, data, 100);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_read_pipe_eof(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[50];
    bc_core_fill(data, 50, (unsigned char)0xCD);

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    ssize_t written = write(pipe_fds[1], data, 50);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 50);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_read_pipe_multiple_chunks(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE + 100;
    unsigned char* data = test_calloc(1, total_size);
    for (size_t i = 0; i < total_size; i++) {
        data[i] = (unsigned char)(i & 0xFF);
    }

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    ssize_t written = write(pipe_fds[1], data, total_size);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    size_t total_read = 0;
    int chunk_count = 0;
    bc_io_stream_chunk_t chunk;

    while (bc_io_stream_read_chunk(stream, &chunk)) {
        assert_true(chunk.size > 0);
        total_read += chunk.size;
        chunk_count++;
    }

    assert_int_equal((int)total_read, (int)total_size);
    assert_true(chunk_count >= 4);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
    test_free(data);
}

static void test_read_pipe_total_size_unknown(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    size_t total_size;
    assert_false(bc_io_stream_total_size(stream, &total_size));

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_read_pipe_remaining_unknown(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    size_t remaining;
    assert_false(bc_io_stream_remaining_bytes(stream, &remaining));

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_read_pipe_source_type(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_PIPE);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_pipe_basic),
        cmocka_unit_test(test_read_pipe_eof),
        cmocka_unit_test(test_read_pipe_multiple_chunks),
        cmocka_unit_test(test_read_pipe_total_size_unknown),
        cmocka_unit_test(test_read_pipe_remaining_unknown),
        cmocka_unit_test(test_read_pipe_source_type),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
