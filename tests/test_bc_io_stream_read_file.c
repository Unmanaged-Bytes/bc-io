// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <stdio.h>
#include <stdlib.h>
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

static char* create_temp_file_with_data(const void* data, size_t size)
{
    char* path = test_calloc(1, 64);
    bc_core_copy(path, "/tmp/bc_io_stream_test_XXXXXX", sizeof("/tmp/bc_io_stream_test_XXXXXX"));
    int file_descriptor = mkstemp(path);
    if (file_descriptor < 0) {
        test_free(path);
        return NULL;
    }
    if (size > 0) {
        ssize_t written = write(file_descriptor, data, size);
        (void)written;
    }
    close(file_descriptor);
    return path;
}

static void test_read_file_small(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xAB);

    char* path = create_temp_file_with_data(data, 100);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);
    assert_true(chunk.owned);
    assert_memory_equal(chunk.data, data, 100);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_exact_buffer(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[TEST_BUFFER_SIZE];
    bc_core_fill(data, TEST_BUFFER_SIZE, (unsigned char)0xCD);

    char* path = create_temp_file_with_data(data, TEST_BUFFER_SIZE);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, TEST_BUFFER_SIZE);
    assert_memory_equal(chunk.data, data, TEST_BUFFER_SIZE);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_multiple_chunks(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE;
    unsigned char data[3 * TEST_BUFFER_SIZE];
    for (size_t i = 0; i < total_size; i++) {
        data[i] = (unsigned char)(i & 0xFF);
    }

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t total_read = 0;
    int chunk_count = 0;
    bc_io_stream_chunk_t chunk;

    while (bc_io_stream_read_chunk(stream, &chunk)) {
        assert_true(chunk.size > 0);
        total_read += chunk.size;
        chunk_count++;
    }

    assert_int_equal((int)total_read, (int)total_size);
    assert_int_equal(chunk_count, 3);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_empty(void** state)
{
    struct test_fixture* fixture = *state;

    char* path = create_temp_file_with_data(NULL, 0);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_large(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 1024 * 1024;
    unsigned char* data = test_calloc(1, total_size);
    bc_core_fill(data, total_size, (unsigned char)0xEF);

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 0, &stream));

    size_t total_read = 0;
    bc_io_stream_chunk_t chunk;

    while (bc_io_stream_read_chunk(stream, &chunk)) {
        total_read += chunk.size;
    }

    assert_int_equal((int)total_read, (int)total_size);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(data);
    test_free(path);
}

static void test_read_file_position_tracking(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE;
    unsigned char data[3 * TEST_BUFFER_SIZE];
    bc_core_fill(data, total_size, (unsigned char)0x11);

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t position;
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, 0);

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, TEST_BUFFER_SIZE);

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, 2 * TEST_BUFFER_SIZE);

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, 3 * TEST_BUFFER_SIZE);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_remaining_tracking(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE;
    unsigned char data[3 * TEST_BUFFER_SIZE];
    bc_core_fill(data, total_size, (unsigned char)0x22);

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t remaining;
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, (int)total_size);

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, (int)(total_size - TEST_BUFFER_SIZE));

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, (int)(total_size - 2 * TEST_BUFFER_SIZE));

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, 0);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_file_stats_tracking(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE;
    unsigned char data[3 * TEST_BUFFER_SIZE];
    bc_core_fill(data, total_size, (unsigned char)0x33);

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
    }

    bc_io_stream_stats_t stats;
    bc_io_stream_get_stats(stream, &stats);
    assert_int_equal((int)stats.bytes_read, (int)total_size);
    assert_int_equal((int)stats.read_count, 3);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_on_write_stream(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0x44);

    char* path = create_temp_file_with_data(data, 100);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_WRITE, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_file_small),
        cmocka_unit_test(test_read_file_exact_buffer),
        cmocka_unit_test(test_read_file_multiple_chunks),
        cmocka_unit_test(test_read_file_empty),
        cmocka_unit_test(test_read_file_large),
        cmocka_unit_test(test_read_file_position_tracking),
        cmocka_unit_test(test_read_file_remaining_tracking),
        cmocka_unit_test(test_read_file_stats_tracking),
        cmocka_unit_test(test_read_on_write_stream),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
