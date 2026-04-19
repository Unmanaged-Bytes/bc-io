// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_stream_internal.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <setjmp.h>
#include <stddef.h>

#include <cmocka.h>

/* ===== Mock infrastructure: open ===== */

static bool mock_open_should_fail = false;
static int mock_open_errno = 0;
static int mock_open_fake_fd = 42;

int __real_open64(const char* pathname, int flags, ...);

int __wrap_open64(const char* pathname, int flags, ...)
{
    if (mock_open_should_fail) {
        errno = mock_open_errno;
        return -1;
    }

    va_list args;
    va_start(args, flags);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        mode = va_arg(args, mode_t);
    }
    va_end(args);
    return __real_open64(pathname, flags, mode);
}

/* ===== Mock infrastructure: fstat64 (_FILE_OFFSET_BITS=64 redirects fstat)
 * ===== */

static bool mock_fstat_should_fail = false;

int __real_fstat64(int file_descriptor, struct stat* stat_buffer);

int __wrap_fstat64(int file_descriptor, struct stat* stat_buffer)
{
    if (mock_fstat_should_fail) {
        errno = EIO;
        return -1;
    }
    return __real_fstat64(file_descriptor, stat_buffer);
}

/* ===== Mock infrastructure: posix_fadvise ===== */

static bool mock_fadvise_should_fail = false;
static int mock_fadvise_return_value = 0;

int __real_posix_fadvise64(int file_descriptor, off_t offset, off_t length, int advice);

int __wrap_posix_fadvise64(int file_descriptor, off_t offset, off_t length, int advice)
{
    if (mock_fadvise_should_fail) {
        return mock_fadvise_return_value;
    }
    return __real_posix_fadvise64(file_descriptor, offset, length, advice);
}

/* ===== Reset ===== */

static void reset_mocks(void)
{
    mock_open_should_fail = false;
    mock_open_errno = 0;
    mock_open_fake_fd = 42;

    mock_fstat_should_fail = false;

    mock_fadvise_should_fail = false;
    mock_fadvise_return_value = 0;
}

/* ===== Helper: create a temporary file for testing ===== */

static char temporary_file_path[256];

static void create_temporary_file(void)
{
    snprintf(temporary_file_path, sizeof(temporary_file_path), "/tmp/bc_io_stream_open_wrap_XXXXXX");
    int fd = mkstemp(temporary_file_path);
    assert_true(fd >= 0);
    const char* content = "test data for stream open wrap";
    ssize_t written = write(fd, content, strlen(content));
    assert_true(written > 0);
    close(fd);
}

static void remove_temporary_file(void)
{
    unlink(temporary_file_path);
}

/* ===== Test: open fails with ENOENT ===== */

static void test_open_file_open_fails(void** state)
{
    (void)state;
    reset_mocks();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_open_should_fail = true;
    mock_open_errno = ENOENT;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, "/nonexistent/path", BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    mock_open_should_fail = false;
    bc_allocators_context_destroy(memory_context);
}

/* ===== Test: fstat fails -> total_size_known is false ===== */

static void test_open_file_fstat_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_fstat_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, temporary_file_path, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_true(result);
    assert_non_null(stream);
    assert_false(stream->total_size_known);

    bc_io_stream_close(stream);
    mock_fstat_should_fail = false;
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: posix_fadvise fails -> stream opens normally ===== */

static void test_open_file_fadvise_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_fadvise_should_fail = true;
    mock_fadvise_return_value = EINVAL;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, temporary_file_path, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_true(result);
    assert_non_null(stream);
    assert_true(stream->total_size_known);

    size_t total_size = 0;
    assert_true(bc_io_stream_total_size(stream, &total_size));
    assert_true(total_size > 0);

    bc_io_stream_close(stream);
    mock_fadvise_should_fail = false;
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: open fails with EACCES (write mode) ===== */

static void test_open_file_open_fails_eacces_write(void** state)
{
    (void)state;
    reset_mocks();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_open_should_fail = true;
    mock_open_errno = EACCES;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, "/readonly/path", BC_IO_STREAM_MODE_WRITE, 0, &stream);

    assert_false(result);

    mock_open_should_fail = false;
    bc_allocators_context_destroy(memory_context);
}

/* ===== Test: fstat fails on fd-based open ===== */

static void test_open_file_descriptor_fstat_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    int fd = __real_open64(temporary_file_path, O_RDONLY | O_CLOEXEC);
    assert_true(fd >= 0);

    mock_fstat_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file_descriptor(memory_context, fd, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_true(result);
    assert_non_null(stream);
    assert_false(stream->total_size_known);

    bc_io_stream_close(stream);
    close(fd);
    mock_fstat_should_fail = false;
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: fadvise fails on fd-based open ===== */

static void test_open_file_descriptor_fadvise_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    int fd = __real_open64(temporary_file_path, O_RDONLY | O_CLOEXEC);
    assert_true(fd >= 0);

    mock_fadvise_should_fail = true;
    mock_fadvise_return_value = EINVAL;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file_descriptor(memory_context, fd, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_true(result);
    assert_non_null(stream);
    assert_true(stream->total_size_known);

    bc_io_stream_close(stream);
    close(fd);
    mock_fadvise_should_fail = false;
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== main ===== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_open_file_open_fails),
        cmocka_unit_test(test_open_file_fstat_fails),
        cmocka_unit_test(test_open_file_fadvise_fails),
        cmocka_unit_test(test_open_file_open_fails_eacces_write),
        cmocka_unit_test(test_open_file_descriptor_fstat_fails),
        cmocka_unit_test(test_open_file_descriptor_fadvise_fails),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
