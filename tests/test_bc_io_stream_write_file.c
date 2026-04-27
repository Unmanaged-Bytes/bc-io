// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

static const bc_allocators_context_config_t default_config = {0};

static bool verify_file_content(const char* path, const void* expected_data, size_t expected_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    struct stat st;
    fstat(fd, &st);
    if ((size_t)st.st_size != expected_size) {
        close(fd);
        return false;
    }

    unsigned char* buffer = malloc(expected_size);
    size_t total = 0;
    while (total < expected_size) {
        ssize_t r = read(fd, buffer + total, expected_size - total);
        if (r <= 0)
            break;
        total += (size_t)r;
    }
    close(fd);

    bool equal = false;
    if (total == expected_size) {
        bc_core_equal(buffer, expected_data, expected_size, &equal);
    }
    free(buffer);
    return equal;
}

static void test_write_file_small(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_small.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bool opened = bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);
    assert_true(opened);

    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xAB);

    size_t bytes_written = 0;
    bool result = bc_io_stream_write_chunk(stream, data, 100, &bytes_written);
    assert_true(result);
    assert_int_equal((int)bytes_written, 100);

    bc_io_stream_close(stream);

    assert_true(verify_file_content(path, data, 100));

    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_file_large(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_large.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bool opened = bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);
    assert_true(opened);

    size_t chunk_size = 4096;
    unsigned char* chunk = malloc(chunk_size);
    bc_core_fill(chunk, chunk_size, (unsigned char)0xCD);

    size_t total_size = 1024 * 1024;
    size_t total_written = 0;

    while (total_written < total_size) {
        size_t bytes_written = 0;
        bool result = bc_io_stream_write_chunk(stream, chunk, chunk_size, &bytes_written);
        assert_true(result);
        total_written += bytes_written;
    }

    bc_io_stream_close(stream);

    struct stat st;
    int stat_result = stat(path, &st);
    assert_int_equal(stat_result, 0);
    assert_int_equal((int)st.st_size, (int)total_size);

    free(chunk);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_file_out_bytes_written(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_out.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);

    unsigned char data[256];
    bc_core_fill(data, 256, (unsigned char)0x42);

    size_t bytes_written = 999;
    bool result = bc_io_stream_write_chunk(stream, data, 256, &bytes_written);
    assert_true(result);
    assert_int_equal((int)bytes_written, 256);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_file_zero_size(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_zero.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);

    const unsigned char data[1] = {0};
    size_t bytes_written = 999;
    bool result = bc_io_stream_write_chunk(stream, data, 0, &bytes_written);
    assert_true(result);
    assert_int_equal((int)bytes_written, 0);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_file_stats(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_stats.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xFF);

    size_t bytes_written = 0;
    bc_io_stream_write_chunk(stream, data, 64, &bytes_written);
    bc_io_stream_write_chunk(stream, data, 64, &bytes_written);
    bc_io_stream_write_chunk(stream, data, 64, &bytes_written);

    bc_io_stream_stats_t stats;
    bc_io_stream_get_stats(stream, &stats);

    assert_int_equal((int)stats.bytes_written, 192);
    assert_int_equal((int)stats.write_count, 3);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_on_read_stream(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_on_read.bin";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ssize_t _w1 = write(fd, "hello", 5);
    (void)_w1;
    close(fd);

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_READ, 0, &stream);

    unsigned char data[10];
    bc_core_fill(data, 10, (unsigned char)0xAA);
    size_t bytes_written = 999;
    bool result = bc_io_stream_write_chunk(stream, data, 10, &bytes_written);
    assert_false(result);
    assert_int_equal((int)bytes_written, 0);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_memory_stream(void** state)
{
    (void)state;

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    unsigned char memory_data[64];
    bc_core_fill(memory_data, 64, (unsigned char)0xBB);

    bc_io_stream_t* stream;
    bc_io_stream_open_memory(memory_context, memory_data, 64, &stream);

    unsigned char write_data[10];
    bc_core_fill(write_data, 10, (unsigned char)0xCC);
    size_t bytes_written = 999;
    bool result = bc_io_stream_write_chunk(stream, write_data, 10, &bytes_written);
    assert_false(result);
    assert_int_equal((int)bytes_written, 0);

    bc_io_stream_close(stream);
    bc_allocators_context_destroy(memory_context);
}

static void test_flush_write_stream(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_flush_write.bin";

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);

    bool result = bc_io_stream_flush(stream);
    assert_true(result);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_flush_read_stream(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_flush_read.bin";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ssize_t _w2 = write(fd, "data", 4);
    (void)_w2;
    close(fd);

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_READ, 0, &stream);

    bool result = bc_io_stream_flush(stream);
    assert_true(result);

    bc_io_stream_close(stream);
    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_creates_file(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_creates.bin";

    unlink(path);

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bool opened = bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);
    assert_true(opened);

    unsigned char data[16];
    bc_core_fill(data, 16, (unsigned char)0xDE);
    size_t bytes_written = 0;
    bc_io_stream_write_chunk(stream, data, 16, &bytes_written);

    bc_io_stream_close(stream);

    struct stat st;
    int stat_result = stat(path, &st);
    assert_int_equal(stat_result, 0);
    assert_true(verify_file_content(path, data, 16));

    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

static void test_write_truncates_file(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_stream_test_write_truncates.bin";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    unsigned char old_data[1024];
    bc_core_fill(old_data, 1024, (unsigned char)0xEE);
    ssize_t _w3 = write(fd, old_data, 1024);
    (void)_w3;
    close(fd);

    bc_allocators_context_t* memory_context;
    bc_allocators_context_create(&default_config, &memory_context);

    bc_io_stream_t* stream;
    bc_io_stream_open_file(memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream);

    unsigned char new_data[10];
    bc_core_fill(new_data, 10, (unsigned char)0x11);
    size_t bytes_written = 0;
    bc_io_stream_write_chunk(stream, new_data, 10, &bytes_written);

    bc_io_stream_close(stream);

    struct stat st;
    stat(path, &st);
    assert_int_equal((int)st.st_size, 10);
    assert_true(verify_file_content(path, new_data, 10));

    unlink(path);
    bc_allocators_context_destroy(memory_context);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_write_file_small),
        cmocka_unit_test(test_write_file_large),
        cmocka_unit_test(test_write_file_out_bytes_written),
        cmocka_unit_test(test_write_file_zero_size),
        cmocka_unit_test(test_write_file_stats),
        cmocka_unit_test(test_write_on_read_stream),
        cmocka_unit_test(test_write_memory_stream),
        cmocka_unit_test(test_flush_write_stream),
        cmocka_unit_test(test_flush_read_stream),
        cmocka_unit_test(test_write_creates_file),
        cmocka_unit_test(test_write_truncates_file),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
