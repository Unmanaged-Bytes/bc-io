// SPDX-License-Identifier: MIT

#include "bc_core.h"
#include "bc_io_file.h"
#include "bc_allocators.h"
#include "bc_io_stream.h"

#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>

#define SMALL_FILE_SIZE ((size_t)1024)
#define LARGE_FILE_SIZE ((size_t)(512 * 1024))

typedef struct fixture {
    bc_allocators_context_t* memory_context;
    char root_directory[256];
    char small_file_path[512];
    char large_file_path[512];
} fixture_t;

static void write_pattern_file(const char* path, size_t size)
{
    int file_descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    unsigned char buffer[4096];
    for (size_t index = 0; index < sizeof(buffer); ++index) {
        buffer[index] = (unsigned char)(index & 0xff);
    }
    size_t written = 0;
    while (written < size) {
        size_t to_write = size - written;
        if (to_write > sizeof(buffer)) {
            to_write = sizeof(buffer);
        }
        ssize_t wrote_now = write(file_descriptor, buffer, to_write);
        assert_true(wrote_now > 0);
        written += (size_t)wrote_now;
    }
    close(file_descriptor);
}

static int setup_open_fixture(void** state)
{
    fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);

    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));

    snprintf(fixture->root_directory, sizeof(fixture->root_directory), "/tmp/bc_io_file_open_%d_%ld", (int)getpid(), (long)time(NULL));
    assert_int_equal(mkdir(fixture->root_directory, 0755), 0);

    snprintf(fixture->small_file_path, sizeof(fixture->small_file_path), "%s/small.bin", fixture->root_directory);
    write_pattern_file(fixture->small_file_path, SMALL_FILE_SIZE);

    snprintf(fixture->large_file_path, sizeof(fixture->large_file_path), "%s/large.bin", fixture->root_directory);
    write_pattern_file(fixture->large_file_path, LARGE_FILE_SIZE);

    *state = fixture;
    return 0;
}

static int teardown_open_fixture(void** state)
{
    fixture_t* fixture = *state;
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf %s", fixture->root_directory);
    int _sys_rc = system(command);
    (void)_sys_rc;
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

/* ========== open_read ========== */

static void test_open_read_regular(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &options, &stream));
    assert_non_null(stream);

    size_t total_size = 0;
    assert_true(bc_io_stream_total_size(stream, &total_size));
    assert_int_equal(total_size, SMALL_FILE_SIZE);

    size_t bytes_read = 0;
    while (bytes_read < SMALL_FILE_SIZE) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk) || chunk.size == 0) {
            break;
        }
        bytes_read += chunk.size;
    }
    assert_int_equal(bytes_read, SMALL_FILE_SIZE);

    bc_io_stream_close(stream);
}

static void test_open_read_noatime_owned(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.use_noatime = true;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &options, &stream));
    assert_non_null(stream);
    bc_io_stream_close(stream);
}

static void test_open_read_noatime_not_owned(void** state)
{
    fixture_t* fixture = *state;
    /* Read-only file simulates not-owned (noatime would EPERM); fallback
   * must succeed silently. */
    chmod(fixture->small_file_path, 0444);
    bc_io_file_open_options_t options = {0};
    options.use_noatime = true;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &options, &stream));
    assert_non_null(stream);
    bc_io_stream_close(stream);
    chmod(fixture->small_file_path, 0644);
}

static void test_open_read_zero_initialized_options(void** state)
{
    fixture_t* fixture = *state;
    bc_io_stream_t* stream = NULL;
    const bc_io_file_open_options_t defaults = {0};
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &defaults, &stream));
    assert_non_null(stream);
    bc_io_stream_close(stream);
}

/* ========== open_auto ========== */

static void test_open_auto_small_file_uses_buffered(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_file_read_handle_t* handle = NULL;
    assert_true(bc_io_file_open_auto(fixture->memory_context, fixture->small_file_path, BC_IO_MMAP_DEFAULT_THRESHOLD, &options, &handle));
    assert_non_null(handle);

    bool is_mapped = true;
    assert_true(bc_io_file_read_handle_is_memory_mapped(handle, &is_mapped));
    assert_false(is_mapped);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_read_handle_get_stream(handle, &stream));
    assert_non_null(stream);

    size_t size = 0;
    assert_true(bc_io_file_read_handle_get_size(handle, &size));
    assert_int_equal(size, SMALL_FILE_SIZE);

    bc_io_file_read_handle_destroy(handle);
}

static void test_open_auto_large_file_uses_mmap(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_file_read_handle_t* handle = NULL;
    assert_true(bc_io_file_open_auto(fixture->memory_context, fixture->large_file_path, BC_IO_MMAP_DEFAULT_THRESHOLD, &options, &handle));
    assert_non_null(handle);

    bool is_mapped = false;
    assert_true(bc_io_file_read_handle_is_memory_mapped(handle, &is_mapped));
    assert_true(is_mapped);

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_read_handle_get_stream(handle, &stream));
    assert_non_null(stream);

    size_t size = 0;
    assert_true(bc_io_file_read_handle_get_size(handle, &size));
    assert_int_equal(size, LARGE_FILE_SIZE);

    bc_io_file_read_handle_destroy(handle);
}

static void test_open_auto_threshold_zero_uses_default(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_file_read_handle_t* handle = NULL;
    assert_true(bc_io_file_open_auto(fixture->memory_context, fixture->small_file_path, 0, &options, &handle));
    assert_non_null(handle);

    bool is_mapped = true;
    assert_true(bc_io_file_read_handle_is_memory_mapped(handle, &is_mapped));
    /* Small file < default 256 KiB -> buffered. */
    assert_false(is_mapped);

    bc_io_file_read_handle_destroy(handle);
}

static void test_read_handle_destroy_releases_resources(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_file_read_handle_t* handle = NULL;
    assert_true(bc_io_file_open_auto(fixture->memory_context, fixture->large_file_path, BC_IO_MMAP_DEFAULT_THRESHOLD, &options, &handle));
    assert_non_null(handle);
    bc_io_file_read_handle_destroy(handle);
}

/* ===== Error-path and branch tests ===== */

static void test_open_read_missing_file_returns_false(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_stream_t* stream = NULL;
    /* No use_noatime -> goes through the direct open() path inside
       resolve_descriptor. The missing file triggers the open < 0 return. */
    assert_false(bc_io_file_open_read(fixture->memory_context, "/tmp/bc_io_file_no_such_file_xyz_12345", &options, &stream));
}

static void test_open_read_with_explicit_buffer_size(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.buffer_size = 8192;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &options, &stream));
    bc_io_stream_close(stream);
}

static void test_open_read_with_nonblock_flag(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.nonblock = true;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_file_open_read(fixture->memory_context, fixture->small_file_path, &options, &stream));
    bc_io_stream_close(stream);
}

static void test_open_auto_missing_file_returns_false(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    bc_io_file_read_handle_t* handle = NULL;
    assert_false(bc_io_file_open_auto(fixture->memory_context, "/tmp/bc_io_file_no_such_file_xyz_12345", BC_IO_MMAP_DEFAULT_THRESHOLD,
                                      &options, &handle));
}

static void test_open_auto_buffered_with_explicit_buffer_size(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.buffer_size = 4096;
    bc_io_file_read_handle_t* handle = NULL;
    assert_true(bc_io_file_open_auto(fixture->memory_context, fixture->small_file_path, BC_IO_MMAP_DEFAULT_THRESHOLD, &options, &handle));
    bool is_mapped = true;
    assert_true(bc_io_file_read_handle_is_memory_mapped(handle, &is_mapped));
    assert_false(is_mapped);
    bc_io_file_read_handle_destroy(handle);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_open_read_regular, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_noatime_owned, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_noatime_not_owned, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_zero_initialized_options, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_auto_small_file_uses_buffered, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_auto_large_file_uses_mmap, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_auto_threshold_zero_uses_default, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_read_handle_destroy_releases_resources, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_missing_file_returns_false, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_with_explicit_buffer_size, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_read_with_nonblock_flag, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_auto_missing_file_returns_false, setup_open_fixture, teardown_open_fixture),
        cmocka_unit_test_setup_teardown(test_open_auto_buffered_with_explicit_buffer_size, setup_open_fixture, teardown_open_fixture),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
