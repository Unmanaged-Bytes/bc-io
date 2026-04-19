// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#define DEFAULT_MEMORY_CHUNK_SIZE 131072

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

static void test_read_memory_small(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xAA);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 100, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);
    assert_memory_equal(chunk.data, data, 100);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
}

static void test_read_memory_large(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 300000;
    unsigned char* data = test_calloc(1, total_size);
    bc_core_fill(data, total_size, (unsigned char)0xBB);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, total_size, &stream));

    size_t total_read = 0;
    int chunk_count = 0;
    bc_io_stream_chunk_t chunk;

    while (bc_io_stream_read_chunk(stream, &chunk)) {
        total_read += chunk.size;
        chunk_count++;
    }

    assert_int_equal((int)total_read, (int)total_size);
    assert_true(chunk_count >= 3);

    bc_io_stream_close(stream);
    test_free(data);
}

static void test_read_memory_owned_false(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xCC);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 100, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_false(chunk.owned);

    bc_io_stream_close(stream);
}

static void test_read_memory_pointer_into_source(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 2 * DEFAULT_MEMORY_CHUNK_SIZE + 100;
    unsigned char* data = test_calloc(1, total_size);
    for (size_t i = 0; i < total_size; i++) {
        data[i] = (unsigned char)(i & 0xFF);
    }

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, total_size, &stream));

    bc_io_stream_chunk_t chunk;

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_ptr_equal(chunk.data, data);
    assert_int_equal((int)chunk.size, DEFAULT_MEMORY_CHUNK_SIZE);

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_ptr_equal(chunk.data, data + DEFAULT_MEMORY_CHUNK_SIZE);
    assert_int_equal((int)chunk.size, DEFAULT_MEMORY_CHUNK_SIZE);

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_ptr_equal(chunk.data, data + 2 * DEFAULT_MEMORY_CHUNK_SIZE);
    assert_int_equal((int)chunk.size, 100);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
    test_free(data);
}

static void test_read_memory_empty(void** state)
{
    struct test_fixture* fixture = *state;
    const unsigned char data[1] = {0};

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 0, &stream));

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
}

static void test_read_memory_position(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 2 * DEFAULT_MEMORY_CHUNK_SIZE + 100;
    unsigned char* data = test_calloc(1, total_size);
    bc_core_fill(data, total_size, (unsigned char)0xDD);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, total_size, &stream));

    size_t position;
    size_t remaining;

    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, 0);
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, (int)total_size);

    bc_io_stream_chunk_t chunk;

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, DEFAULT_MEMORY_CHUNK_SIZE);
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, (int)(total_size - DEFAULT_MEMORY_CHUNK_SIZE));

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, 2 * DEFAULT_MEMORY_CHUNK_SIZE);

    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    bc_io_stream_current_position(stream, &position);
    assert_int_equal((int)position, (int)total_size);
    bc_io_stream_remaining_bytes(stream, &remaining);
    assert_int_equal((int)remaining, 0);

    bc_io_stream_close(stream);
    test_free(data);
}

static void test_read_memory_is_eof(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xEE);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 100, &stream));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_false(is_eof);

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_false(is_eof);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_memory_small),       cmocka_unit_test(test_read_memory_large),
        cmocka_unit_test(test_read_memory_owned_false), cmocka_unit_test(test_read_memory_pointer_into_source),
        cmocka_unit_test(test_read_memory_empty),       cmocka_unit_test(test_read_memory_position),
        cmocka_unit_test(test_read_memory_is_eof),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
