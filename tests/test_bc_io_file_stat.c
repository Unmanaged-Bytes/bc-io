// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io_file_stat.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_payload_to_path(const char* path, const char* payload, size_t payload_length)
{
    const int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    const ssize_t written = write(file_descriptor, payload, payload_length);
    assert_int_equal((size_t)written, payload_length);
    assert_int_equal(close(file_descriptor), 0);
}

static void test_stat_regular_file(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_file_stat_regular.txt";
    const char* payload = "hello world";
    const size_t payload_length = strlen(payload);
    write_payload_to_path(path, payload, payload_length);

    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat(path, &file_stat));
    assert_true(file_stat.is_regular);
    assert_false(file_stat.is_directory);
    assert_false(file_stat.is_symlink);
    assert_int_equal((size_t)file_stat.size_bytes, payload_length);
    assert_int_equal(file_stat.mode & 07777, file_stat.mode);

    unlink(path);
}

static void test_stat_directory(void** state)
{
    (void)state;
    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat("/tmp", &file_stat));
    assert_true(file_stat.is_directory);
    assert_false(file_stat.is_regular);
    assert_false(file_stat.is_symlink);
}

static void test_stat_symlink_follows(void** state)
{
    (void)state;
    const char* link_path = "/tmp/bc_io_file_stat_symlink_follow";
    unlink(link_path);
    assert_int_equal(symlink("/tmp", link_path), 0);

    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat(link_path, &file_stat));
    assert_false(file_stat.is_symlink);
    assert_true(file_stat.is_directory);

    unlink(link_path);
}

static void test_stat_lstat_does_not_follow(void** state)
{
    (void)state;
    const char* link_path = "/tmp/bc_io_file_stat_symlink_no_follow";
    unlink(link_path);
    assert_int_equal(symlink("/tmp", link_path), 0);

    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat_lstat(link_path, &file_stat));
    assert_true(file_stat.is_symlink);
    assert_false(file_stat.is_directory);

    unlink(link_path);
}

static void test_stat_nonexistent_returns_false(void** state)
{
    (void)state;
    bc_io_file_stat_t file_stat = {0};
    assert_false(bc_io_file_stat("/tmp/bc_io_file_stat_does_not_exist_xyzzy", &file_stat));
    assert_false(bc_io_file_stat_lstat("/tmp/bc_io_file_stat_does_not_exist_xyzzy", &file_stat));
}

static void test_stat_mtime_recent(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_file_stat_mtime.txt";
    write_payload_to_path(path, "x", 1);

    const time_t now = time(NULL);

    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat(path, &file_stat));

    const int64_t delta_seconds = (int64_t)now - file_stat.modification_time_seconds;
    const int64_t absolute_delta = delta_seconds < 0 ? -delta_seconds : delta_seconds;
    assert_true(absolute_delta < 5);

    unlink(path);
}

static void test_stat_mode_excludes_file_type_bits(void** state)
{
    (void)state;
    const char* path = "/tmp/bc_io_file_stat_mode.txt";
    const int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    assert_int_equal(fchmod(file_descriptor, 0640), 0);
    assert_int_equal(close(file_descriptor), 0);

    bc_io_file_stat_t file_stat = {0};
    assert_true(bc_io_file_stat(path, &file_stat));
    assert_int_equal(file_stat.mode, 0640);

    unlink(path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_stat_regular_file),
        cmocka_unit_test(test_stat_directory),
        cmocka_unit_test(test_stat_symlink_follows),
        cmocka_unit_test(test_stat_lstat_does_not_follow),
        cmocka_unit_test(test_stat_nonexistent_returns_false),
        cmocka_unit_test(test_stat_mtime_recent),
        cmocka_unit_test(test_stat_mode_excludes_file_type_bits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
