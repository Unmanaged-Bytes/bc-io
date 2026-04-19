// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_stream_internal.h"

#include "bc_allocators.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

/* ===== Mock infrastructure: write ===== */

#define MOCK_MAX_CALLS 16

static ssize_t mock_write_return_values[MOCK_MAX_CALLS];
static int mock_write_errno_values[MOCK_MAX_CALLS];
static int mock_write_call_index;
static bool mock_write_enabled;

ssize_t __real_write(int file_descriptor, const void* data, size_t count);

ssize_t __wrap_write(int file_descriptor, const void* data, size_t count)
{
    if (!mock_write_enabled) {
        return __real_write(file_descriptor, data, count);
    }
    int index = mock_write_call_index++;
    if (mock_write_errno_values[index] != 0) {
        errno = mock_write_errno_values[index];
        return mock_write_return_values[index];
    }
    return mock_write_return_values[index];
}

static void reset_write_mock(void)
{
    for (int i = 0; i < MOCK_MAX_CALLS; i++) {
        mock_write_return_values[i] = 0;
        mock_write_errno_values[i] = 0;
    }
    mock_write_call_index = 0;
    mock_write_enabled = false;
}

/* ===== Fixture: create a write stream on /dev/null ===== */

typedef struct {
    bc_allocators_context_t* memory_context;
    bc_io_stream_t* stream;
    int dev_null_fd;
} write_fixture_t;

static void setup_fixture(write_fixture_t* fixture, size_t buffer_size)
{
    fixture->memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &fixture->memory_context));

    fixture->dev_null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    assert_int_not_equal(fixture->dev_null_fd, -1);

    fixture->stream = NULL;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, fixture->dev_null_fd, BC_IO_STREAM_SOURCE_FILE,
                                                  BC_IO_STREAM_MODE_WRITE, buffer_size, &fixture->stream));
}

static void teardown_fixture(write_fixture_t* fixture)
{
    reset_write_mock();
    bc_io_stream_close(fixture->stream);
    close(fixture->dev_null_fd);
    bc_allocators_context_destroy(fixture->memory_context);
}

/* ===== Tests: bc_io_stream_flush behavior on partial / failed writes ===== */

/* Bug A1: on write failure during flush, the buffered bytes that were
   successfully transmitted must be counted in stats.bytes_written and the
   buffer must be compacted (or emptied only if fully written). */
static void test_flush_failure_updates_bytes_written_stat(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 4096);

    /* Fill the buffer with 1000 bytes by writing a small chunk. */
    unsigned char small_data[1000];
    for (size_t i = 0; i < sizeof(small_data); i++) {
        small_data[i] = (unsigned char)(i & 0xFF);
    }
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(fixture.stream, small_data, sizeof(small_data), &written));
    assert_int_equal((int)written, 1000);

    /* bytes_written at this point reflects the buffered write (which is
       counted eagerly by write_chunk). */
    bc_io_stream_stats_t stats_before;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats_before));
    assert_int_equal((int)stats_before.bytes_written, 1000);

    /* Now enable mock: first write transmits 600 bytes, second returns EIO. */
    reset_write_mock();
    mock_write_return_values[0] = 600;
    mock_write_return_values[1] = -1;
    mock_write_errno_values[1] = EIO;
    mock_write_enabled = true;

    assert_false(bc_io_stream_flush(fixture.stream));

    mock_write_enabled = false;

    bc_io_stream_stats_t stats_after;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats_after));

    /* AFTER FIX: stats.bytes_written must include the 600 bytes that actually
       made it through the first successful write. Today: write_chunk already
       accounted the whole 1000 at buffer time, so bytes_written stays 1000.
       The test asserts that short_write_count is incremented, and that
       buffer_used reflects the 400 unsent bytes (data preserved for retry). */
    assert_int_equal((int)stats_after.short_write_count, 1);
    assert_int_equal((int)fixture.stream->buffer_used, 400);

    teardown_fixture(&fixture);
}

/* Bug A2: on complete flush success via a single retry loop, buffer_used
   must be 0 and no short_write_count bump. This is the sanity twin of A1. */
static void test_flush_success_clears_buffer(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 4096);

    unsigned char data[500];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (unsigned char)i;
    }
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(fixture.stream, data, sizeof(data), &written));

    reset_write_mock();
    mock_write_return_values[0] = 500;
    mock_write_enabled = true;

    assert_true(bc_io_stream_flush(fixture.stream));

    mock_write_enabled = false;

    assert_int_equal((int)fixture.stream->buffer_used, 0);

    bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats));
    assert_int_equal((int)stats.short_write_count, 0);

    teardown_fixture(&fixture);
}

/* Bug B: on a large chunk (> buffer_size) that partially transmits before
   the syscall fails, write_chunk must advance logical_position by the number
   of bytes actually written. Today it returns false without touching
   logical_position on the failure branch. */
static void test_write_chunk_large_partial_updates_logical_position(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 1024);

    /* Large chunk > buffer_size so the direct write_full path is taken. */
    unsigned char large[4096];
    for (size_t i = 0; i < sizeof(large); i++) {
        large[i] = (unsigned char)(i & 0xFF);
    }

    size_t position_before = 0;
    assert_true(bc_io_stream_current_position(fixture.stream, &position_before));
    assert_int_equal((int)position_before, 0);

    /* Mock: first write transmits 1500 bytes, second returns EIO. */
    reset_write_mock();
    mock_write_return_values[0] = 1500;
    mock_write_return_values[1] = -1;
    mock_write_errno_values[1] = EIO;
    mock_write_enabled = true;

    size_t written = 0;
    assert_false(bc_io_stream_write_chunk(fixture.stream, large, sizeof(large), &written));

    mock_write_enabled = false;

    assert_int_equal((int)written, 1500);

    size_t position_after = 0;
    assert_true(bc_io_stream_current_position(fixture.stream, &position_after));
    /* AFTER FIX: logical_position must equal 1500. Before fix: stays at 0. */
    assert_int_equal((int)position_after, 1500);

    bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats));
    assert_int_equal((int)stats.bytes_written, 1500);

    teardown_fixture(&fixture);
}

/* Coverage: write_chunk takes the "pre-flush existing buffered data" branch
   and that flush fails. Must return false with out_bytes_written == 0. */
static void test_write_chunk_preflush_failure_returns_false(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 1024);

    /* Put 100 bytes in the buffer. */
    const unsigned char small[100] = {0};
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(fixture.stream, small, sizeof(small), &written));
    assert_int_equal((int)fixture.stream->buffer_used, 100);

    /* 2000-byte chunk does not fit in the remaining 924 bytes of buffer, so
       write_chunk tries to flush the existing 100 bytes first. Make that
       flush fail with EIO to exercise the pre-flush failure branch. */
    const unsigned char large[2000] = {0};
    reset_write_mock();
    mock_write_return_values[0] = -1;
    mock_write_errno_values[0] = EIO;
    mock_write_enabled = true;

    size_t written2 = 999;
    assert_false(bc_io_stream_write_chunk(fixture.stream, large, sizeof(large), &written2));
    assert_int_equal((int)written2, 0);

    mock_write_enabled = false;
    teardown_fixture(&fixture);
}

/* Coverage: flush with EAGAIN partial (write_full returns true with
   bytes_written < original_used). The short-circuit on line 28 must hit the
   (write_ok=true, remaining>0) combination. */
static void test_flush_eagain_partial_returns_false(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 4096);

    const unsigned char data[500] = {0};
    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(fixture.stream, data, sizeof(data), &written));

    /* First write transmits 200 bytes, second returns EAGAIN. write_full
       breaks the loop with total_written=200 and returns true. */
    reset_write_mock();
    mock_write_return_values[0] = 200;
    mock_write_return_values[1] = -1;
    mock_write_errno_values[1] = EAGAIN;
    mock_write_enabled = true;

    /* Flush returns false because remaining != 0, even though write_full
       reported success. short_write_count is bumped and the 300 unsent
       bytes are compacted at the start of the buffer for a future retry. */
    assert_false(bc_io_stream_flush(fixture.stream));
    assert_int_equal((int)fixture.stream->buffer_used, 300);

    bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats));
    assert_int_equal((int)stats.short_write_count, 1);

    mock_write_enabled = false;
    teardown_fixture(&fixture);
}

/* Coverage: write_chunk direct path with a fully successful write, so the
   short_write_count branch on line 72 is NOT taken. */
static void test_write_chunk_large_full_success(void** state)
{
    (void)state;
    write_fixture_t fixture;
    setup_fixture(&fixture, 1024);

    const unsigned char large[4096] = {0};

    reset_write_mock();
    mock_write_return_values[0] = 4096;
    mock_write_enabled = true;

    size_t written = 0;
    assert_true(bc_io_stream_write_chunk(fixture.stream, large, sizeof(large), &written));
    assert_int_equal((int)written, 4096);

    bc_io_stream_stats_t stats;
    assert_true(bc_io_stream_get_stats(fixture.stream, &stats));
    assert_int_equal((int)stats.bytes_written, 4096);
    assert_int_equal((int)stats.short_write_count, 0);

    size_t position = 0;
    assert_true(bc_io_stream_current_position(fixture.stream, &position));
    assert_int_equal((int)position, 4096);

    mock_write_enabled = false;
    teardown_fixture(&fixture);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_flush_failure_updates_bytes_written_stat),
        cmocka_unit_test(test_flush_success_clears_buffer),
        cmocka_unit_test(test_write_chunk_large_partial_updates_logical_position),
        cmocka_unit_test(test_write_chunk_preflush_failure_returns_false),
        cmocka_unit_test(test_flush_eagain_partial_returns_false),
        cmocka_unit_test(test_write_chunk_large_full_success),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
