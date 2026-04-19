// SPDX-License-Identifier: MIT

#include "bc_io_stream_io_internal.h"

#include "bc_core.h"

#include <errno.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

/* ===== Mock infrastructure: read ===== */

#define MOCK_MAX_CALLS 16

static ssize_t mock_read_return_values[MOCK_MAX_CALLS];
static int mock_read_errno_values[MOCK_MAX_CALLS];
static int mock_read_call_index;

ssize_t __real_read(int file_descriptor, void* buffer, size_t count);

ssize_t __wrap_read(int file_descriptor, void* buffer, size_t count)
{
    (void)file_descriptor;
    (void)count;
    int index = mock_read_call_index++;
    if (mock_read_errno_values[index] != 0) {
        errno = mock_read_errno_values[index];
        return mock_read_return_values[index];
    }
    size_t bytes_to_produce = (size_t)mock_read_return_values[index];
    if (bytes_to_produce > 0) {
        bc_core_fill(buffer, bytes_to_produce, (unsigned char)'R');
    }
    return mock_read_return_values[index];
}

/* ===== Mock infrastructure: write ===== */

static ssize_t mock_write_return_values[MOCK_MAX_CALLS];
static int mock_write_errno_values[MOCK_MAX_CALLS];
static int mock_write_call_index;

ssize_t __real_write(int file_descriptor, const void* data, size_t count);

ssize_t __wrap_write(int file_descriptor, const void* data, size_t count)
{
    (void)file_descriptor;
    (void)data;
    (void)count;
    int index = mock_write_call_index++;
    if (mock_write_errno_values[index] != 0) {
        errno = mock_write_errno_values[index];
        return mock_write_return_values[index];
    }
    return mock_write_return_values[index];
}

/* ===== Reset ===== */

static void reset_mocks(void)
{
    bc_core_zero(mock_read_return_values, sizeof(mock_read_return_values));
    bc_core_zero(mock_read_errno_values, sizeof(mock_read_errno_values));
    mock_read_call_index = 0;

    bc_core_zero(mock_write_return_values, sizeof(mock_write_return_values));
    bc_core_zero(mock_write_errno_values, sizeof(mock_write_errno_values));
    mock_write_call_index = 0;
}

/* ===== Tests: read_full ===== */

static void test_read_full_complete_read(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = 64;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 64);
    assert_false(eof);
    assert_int_equal(buffer[0], 'R');
    assert_int_equal(buffer[63], 'R');
}

static void test_read_full_short_reads(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = 10;
    mock_read_return_values[1] = 20;
    mock_read_return_values[2] = 34;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 64);
    assert_false(eof);
}

static void test_read_full_eintr_retry(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = -1;
    mock_read_errno_values[0] = EINTR;
    mock_read_return_values[1] = -1;
    mock_read_errno_values[1] = EINTR;
    mock_read_return_values[2] = 64;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 64);
    assert_false(eof);
}

static void test_read_full_eagain_partial(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = 20;
    mock_read_return_values[1] = -1;
    mock_read_errno_values[1] = EAGAIN;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 20);
    assert_false(eof);
}

static void test_read_full_eof_midway(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = 30;
    mock_read_return_values[1] = 0;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 30);
    assert_true(eof);
}

static void test_read_full_eof_immediate(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = 0;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_true(result);
    assert_int_equal((int)bytes_read, 0);
    assert_true(eof);
}

static void test_read_full_error(void** state)
{
    (void)state;
    reset_mocks();

    mock_read_return_values[0] = -1;
    mock_read_errno_values[0] = EIO;

    unsigned char buffer[64];
    size_t bytes_read = 0;
    bool eof = false;

    bool result = bc_io_stream_io_read_full(42, buffer, 64, &bytes_read, &eof);

    assert_false(result);
    assert_int_equal((int)bytes_read, 0);
}

/* ===== Tests: write_full ===== */

static void test_write_full_complete_write(void** state)
{
    (void)state;
    reset_mocks();

    mock_write_return_values[0] = 64;

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)'W');
    size_t bytes_written = 0;

    bool result = bc_io_stream_io_write_full(42, data, 64, &bytes_written);

    assert_true(result);
    assert_int_equal((int)bytes_written, 64);
}

static void test_write_full_short_writes(void** state)
{
    (void)state;
    reset_mocks();

    mock_write_return_values[0] = 10;
    mock_write_return_values[1] = 30;
    mock_write_return_values[2] = 24;

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)'W');
    size_t bytes_written = 0;

    bool result = bc_io_stream_io_write_full(42, data, 64, &bytes_written);

    assert_true(result);
    assert_int_equal((int)bytes_written, 64);
}

static void test_write_full_eintr_retry(void** state)
{
    (void)state;
    reset_mocks();

    mock_write_return_values[0] = -1;
    mock_write_errno_values[0] = EINTR;
    mock_write_return_values[1] = 64;

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)'W');
    size_t bytes_written = 0;

    bool result = bc_io_stream_io_write_full(42, data, 64, &bytes_written);

    assert_true(result);
    assert_int_equal((int)bytes_written, 64);
}

static void test_write_full_epipe(void** state)
{
    (void)state;
    reset_mocks();

    mock_write_return_values[0] = -1;
    mock_write_errno_values[0] = EPIPE;

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)'W');
    size_t bytes_written = 0;

    bool result = bc_io_stream_io_write_full(42, data, 64, &bytes_written);

    assert_false(result);
    assert_int_equal((int)bytes_written, 0);
}

static void test_write_full_error(void** state)
{
    (void)state;
    reset_mocks();

    mock_write_return_values[0] = -1;
    mock_write_errno_values[0] = EIO;

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)'W');
    size_t bytes_written = 0;

    bool result = bc_io_stream_io_write_full(42, data, 64, &bytes_written);

    assert_false(result);
    assert_int_equal((int)bytes_written, 0);
}

/* ===== main ===== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_full_complete_read), cmocka_unit_test(test_read_full_short_reads),
        cmocka_unit_test(test_read_full_eintr_retry),   cmocka_unit_test(test_read_full_eagain_partial),
        cmocka_unit_test(test_read_full_eof_midway),    cmocka_unit_test(test_read_full_eof_immediate),
        cmocka_unit_test(test_read_full_error),         cmocka_unit_test(test_write_full_complete_write),
        cmocka_unit_test(test_write_full_short_writes), cmocka_unit_test(test_write_full_eintr_retry),
        cmocka_unit_test(test_write_full_epipe),        cmocka_unit_test(test_write_full_error),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
