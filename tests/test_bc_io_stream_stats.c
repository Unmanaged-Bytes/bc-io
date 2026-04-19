// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

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
    bc_core_copy(path, "/tmp/bc_io_stream_stat_XXXXXX", sizeof("/tmp/bc_io_stream_stat_XXXXXX"));
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

static void test_stats_initial_zero(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xAA);

    char* path = create_temp_file_with_data(data, 64);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    __attribute__((aligned(64))) bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(stream, &stats));
    assert_int_equal((int)stats.bytes_read, 0);
    assert_int_equal((int)stats.bytes_written, 0);
    assert_int_equal((int)stats.read_count, 0);
    assert_int_equal((int)stats.write_count, 0);
    assert_int_equal((int)stats.short_read_count, 0);
    assert_int_equal((int)stats.short_write_count, 0);
    assert_int_equal((int)stats.retry_count, 0);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_stats_after_reads(void** state)
{
    struct test_fixture* fixture = *state;
    size_t total_size = 3 * TEST_BUFFER_SIZE;
    unsigned char data[3 * TEST_BUFFER_SIZE];
    bc_core_fill(data, total_size, (unsigned char)0xBB);

    char* path = create_temp_file_with_data(data, total_size);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
    }

    __attribute__((aligned(64))) bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(stream, &stats));
    assert_int_equal((int)stats.bytes_read, (int)total_size);
    assert_int_equal((int)stats.read_count, 3);
    assert_int_equal((int)stats.bytes_written, 0);
    assert_int_equal((int)stats.write_count, 0);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_stats_after_writes(void** state)
{
    struct test_fixture* fixture = *state;
    const char* path = "/tmp/bc_io_stream_test_stats_writes.bin";

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_WRITE, TEST_BUFFER_SIZE, &stream));

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xCC);

    size_t bytes_written;
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));

    __attribute__((aligned(64))) bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(stream, &stats));
    assert_int_equal((int)stats.bytes_written, 192);
    assert_int_equal((int)stats.write_count, 3);
    assert_int_equal((int)stats.bytes_read, 0);
    assert_int_equal((int)stats.read_count, 0);

    bc_io_stream_close(stream);
    unlink(path);
}

static void test_stats_short_read(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xDD);

    char* path = create_temp_file_with_data(data, 100);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);

    __attribute__((aligned(64))) bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(stream, &stats));
    assert_int_equal((int)stats.short_read_count, 1);
    assert_int_equal((int)stats.read_count, 1);
    assert_int_equal((int)stats.bytes_read, 100);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_stats_initial_zero),
        cmocka_unit_test(test_stats_after_reads),
        cmocka_unit_test(test_stats_after_writes),
        cmocka_unit_test(test_stats_short_read),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
