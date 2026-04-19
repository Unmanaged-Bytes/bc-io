// SPDX-License-Identifier: MIT

#include "bc_core.h"
#include "bc_io_file.h"
#include "bc_allocators.h"

#include <dirent.h>
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

typedef struct fixture {
    bc_allocators_context_t* memory_context;
    char root_directory[256];
    char regular_file_path[512];
    char subdirectory_path[512];
} fixture_t;

static size_t length_of_cstring(const char* text)
{
    size_t out_length = 0;
    bc_core_length(text, '\0', &out_length);
    return out_length;
}

static void write_file_with_bytes(const char* path, size_t size)
{
    int file_descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    if (size > 0) {
        char buffer[256];
        bc_core_fill(buffer, sizeof(buffer), 'A');
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
    }
    close(file_descriptor);
}

static int setup_fixture(void** state)
{
    fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);

    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));

    snprintf(fixture->root_directory, sizeof(fixture->root_directory), "/tmp/bc_io_file_path_%d_%ld", (int)getpid(), (long)time(NULL));
    assert_int_equal(mkdir(fixture->root_directory, 0755), 0);

    snprintf(fixture->regular_file_path, sizeof(fixture->regular_file_path), "%s/regular.txt", fixture->root_directory);
    write_file_with_bytes(fixture->regular_file_path, 128);

    snprintf(fixture->subdirectory_path, sizeof(fixture->subdirectory_path), "%s/subdir", fixture->root_directory);
    assert_int_equal(mkdir(fixture->subdirectory_path, 0755), 0);

    *state = fixture;
    return 0;
}

static int teardown_fixture(void** state)
{
    fixture_t* fixture = *state;
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf %s", fixture->root_directory);
    (void)system(command);
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

/* ========== path_join ========== */

static void test_path_join_basic(void** state)
{
    (void)state;
    char buffer[64];
    size_t out_length = 0;
    assert_true(bc_io_file_path_join(buffer, sizeof(buffer), "/a", 2, "b", 1, &out_length));
    assert_int_equal(out_length, 4);
    bool equal = false;
    bc_core_equal(buffer, "/a/b", 5, &equal);
    assert_true(equal);
}

static void test_path_join_empty_base(void** state)
{
    (void)state;
    char buffer[64];
    size_t out_length = 0;
    assert_true(bc_io_file_path_join(buffer, sizeof(buffer), "", 0, "name", 4, &out_length));
    assert_int_equal(out_length, 4);
    bool equal = false;
    bc_core_equal(buffer, "name", 5, &equal);
    assert_true(equal);
}

static void test_path_join_trailing_slash_not_normalized(void** state)
{
    (void)state;
    char buffer[64];
    size_t out_length = 0;
    assert_true(bc_io_file_path_join(buffer, sizeof(buffer), "/a/", 3, "b", 1, &out_length));
    bool equal = false;
    bc_core_equal(buffer, "/a//b", 6, &equal);
    assert_true(equal);
    assert_int_equal(out_length, 5);
}

static void test_path_join_overflow(void** state)
{
    (void)state;
    char buffer[8];
    size_t out_length = 0;
    assert_false(bc_io_file_path_join(buffer, sizeof(buffer), "/very/long/base/path", 20, "name.txt", 8, &out_length));
}

static void test_path_join_empty_base_overflow(void** state)
{
    (void)state;
    char buffer[4];
    size_t out_length = 0;
    /* empty base + name that does not fit in the buffer hits the
       base_length == 0 early-return-false branch. */
    assert_false(bc_io_file_path_join(buffer, sizeof(buffer), "", 0, "too_long_name", 13, &out_length));
}

static void test_path_join_both_empty(void** state)
{
    (void)state;
    char buffer[64];
    size_t out_length = 123;
    assert_true(bc_io_file_path_join(buffer, sizeof(buffer), "", 0, "", 0, &out_length));
    assert_int_equal(out_length, 0);
    assert_int_equal(buffer[0], '\0');
}

static void test_path_join_length_without_strlen(void** state)
{
    (void)state;
    char buffer[128];
    const char* base = "/root/child";
    const char* name = "leaf.txt";
    size_t base_len = length_of_cstring(base);
    size_t name_len = length_of_cstring(name);
    size_t out_length = 0;
    assert_true(bc_io_file_path_join(buffer, sizeof(buffer), base, base_len, name, name_len, &out_length));
    assert_int_equal(out_length, base_len + 1 + name_len);
    bool equal = false;
    bc_core_equal(buffer, "/root/child/leaf.txt", 21, &equal);
    assert_true(equal);
}

/* ========== dtype_to_entry_type ========== */

static void test_dtype_regular(void** state)
{
    (void)state;
    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    assert_true(bc_io_file_dtype_to_entry_type(DT_REG, &entry_type));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_FILE);
}

static void test_dtype_directory(void** state)
{
    (void)state;
    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    assert_true(bc_io_file_dtype_to_entry_type(DT_DIR, &entry_type));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_DIRECTORY);
}

static void test_dtype_symlink(void** state)
{
    (void)state;
    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_FILE;
    assert_true(bc_io_file_dtype_to_entry_type(DT_LNK, &entry_type));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_SYMLINK);
}

static void test_dtype_unknown(void** state)
{
    (void)state;
    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_FILE;
    assert_true(bc_io_file_dtype_to_entry_type(DT_UNKNOWN, &entry_type));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_OTHER);
}

static void test_dtype_fifo(void** state)
{
    (void)state;
    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_FILE;
    assert_true(bc_io_file_dtype_to_entry_type(DT_FIFO, &entry_type));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_OTHER);
}

/* ========== open_for_read ========== */

static void test_open_for_read_success(void** state)
{
    const fixture_t* fixture = *state;
    int file_descriptor = -1;
    assert_true(bc_io_file_open_for_read(fixture->regular_file_path, 0, &file_descriptor));
    assert_true(file_descriptor >= 0);
    close(file_descriptor);
}

static void test_open_for_read_missing_file(void** state)
{
    const fixture_t* fixture = *state;
    char missing_path[512];
    snprintf(missing_path, sizeof(missing_path), "%s/does_not_exist.xyz", fixture->root_directory);
    int file_descriptor = -1;
    assert_false(bc_io_file_open_for_read(missing_path, 0, &file_descriptor));
}

static void test_open_for_read_nonblock_flag(void** state)
{
    const fixture_t* fixture = *state;
    int file_descriptor = -1;
    assert_true(bc_io_file_open_for_read(fixture->regular_file_path, O_NONBLOCK, &file_descriptor));
    assert_true(file_descriptor >= 0);
    int flags = fcntl(file_descriptor, F_GETFL);
    assert_true((flags & O_NONBLOCK) != 0);
    close(file_descriptor);
}

/* ========== stat_if_unknown ========== */

static void test_stat_if_unknown_regular_file(void** state)
{
    fixture_t* fixture = *state;
    int directory_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(directory_fd >= 0);

    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    dev_t device = 0;
    ino_t inode = 0;
    size_t file_size = 0;
    time_t modification_time = 0;
    assert_true(bc_io_file_stat_if_unknown(directory_fd, "regular.txt", &entry_type, &device, &inode, &file_size, &modification_time));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_FILE);
    assert_int_equal(file_size, 128);
    assert_true(inode != 0);
    close(directory_fd);
}

static void test_stat_if_unknown_subdirectory(void** state)
{
    fixture_t* fixture = *state;
    int directory_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(directory_fd >= 0);

    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    dev_t device = 0;
    ino_t inode = 0;
    size_t file_size = 0;
    time_t modification_time = 0;
    assert_true(bc_io_file_stat_if_unknown(directory_fd, "subdir", &entry_type, &device, &inode, &file_size, &modification_time));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_DIRECTORY);
    close(directory_fd);
}

static void test_stat_if_unknown_missing_file_returns_false(void** state)
{
    const fixture_t* fixture = *state;
    int directory_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(directory_fd >= 0);

    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    dev_t device = 0;
    ino_t inode = 0;
    size_t file_size = 0;
    time_t modification_time = 0;
    assert_false(
        bc_io_file_stat_if_unknown(directory_fd, "does_not_exist.xyz", &entry_type, &device, &inode, &file_size, &modification_time));

    close(directory_fd);
}

static void test_stat_if_unknown_symlink(void** state)
{
    const fixture_t* fixture = *state;
    char symlink_path[512];
    snprintf(symlink_path, sizeof(symlink_path), "%s/test_symlink", fixture->root_directory);
    (void)unlink(symlink_path);
    assert_int_equal(symlink("regular.txt", symlink_path), 0);

    int directory_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(directory_fd >= 0);

    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
    dev_t device = 0;
    ino_t inode = 0;
    size_t file_size = 0;
    time_t modification_time = 0;
    assert_true(bc_io_file_stat_if_unknown(directory_fd, "test_symlink", &entry_type, &device, &inode, &file_size, &modification_time));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_SYMLINK);

    close(directory_fd);
    (void)unlink(symlink_path);
}

static void test_stat_if_unknown_fifo_is_other(void** state)
{
    const fixture_t* fixture = *state;
    char fifo_path[512];
    snprintf(fifo_path, sizeof(fifo_path), "%s/test_fifo", fixture->root_directory);
    (void)unlink(fifo_path);
    assert_int_equal(mkfifo(fifo_path, 0600), 0);

    int directory_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(directory_fd >= 0);

    bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_FILE;
    dev_t device = 0;
    ino_t inode = 0;
    size_t file_size = 0;
    time_t modification_time = 0;
    assert_true(bc_io_file_stat_if_unknown(directory_fd, "test_fifo", &entry_type, &device, &inode, &file_size, &modification_time));
    assert_int_equal(entry_type, BC_IO_ENTRY_TYPE_OTHER);

    close(directory_fd);
    (void)unlink(fifo_path);
}

/* ========== advise ========== */

static void test_advise_all_hints(void** state)
{
    const fixture_t* fixture = *state;
    int file_descriptor = -1;
    assert_true(bc_io_file_open_for_read(fixture->regular_file_path, 0, &file_descriptor));
    assert_true(file_descriptor >= 0);

    assert_true(bc_io_file_advise(file_descriptor, 0, 128, BC_IO_MADVISE_NORMAL));
    assert_true(bc_io_file_advise(file_descriptor, 0, 128, BC_IO_MADVISE_SEQUENTIAL));
    assert_true(bc_io_file_advise(file_descriptor, 0, 128, BC_IO_MADVISE_RANDOM));
    assert_true(bc_io_file_advise(file_descriptor, 0, 128, BC_IO_MADVISE_WILLNEED));
    assert_true(bc_io_file_advise(file_descriptor, 0, 128, BC_IO_MADVISE_DONTNEED));

    close(file_descriptor);
}

static void test_advise_invalid_hint_returns_false(void** state)
{
    const fixture_t* fixture = *state;
    int file_descriptor = -1;
    assert_true(bc_io_file_open_for_read(fixture->regular_file_path, 0, &file_descriptor));
    assert_true(file_descriptor >= 0);

    /* Cast an out-of-range integer to the enum to reach the default
       branch of the switch (invalid hint -> return false). */
    assert_false(bc_io_file_advise(file_descriptor, 0, 128, (bc_io_mmap_madvise_hint_t)999));

    close(file_descriptor);
}

static void test_advise_invalid_fd_returns_false(void** state)
{
    (void)state;
    /* -1 is never a valid fd, posix_fadvise returns EBADF, our wrapper
       propagates it via errno and returns false. */
    assert_false(bc_io_file_advise(-1, 0, 128, BC_IO_MADVISE_SEQUENTIAL));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_path_join_basic),
        cmocka_unit_test(test_path_join_empty_base),
        cmocka_unit_test(test_path_join_trailing_slash_not_normalized),
        cmocka_unit_test(test_path_join_overflow),
        cmocka_unit_test(test_path_join_empty_base_overflow),
        cmocka_unit_test(test_path_join_both_empty),
        cmocka_unit_test(test_path_join_length_without_strlen),
        cmocka_unit_test(test_dtype_regular),
        cmocka_unit_test(test_dtype_directory),
        cmocka_unit_test(test_dtype_symlink),
        cmocka_unit_test(test_dtype_unknown),
        cmocka_unit_test(test_dtype_fifo),
        cmocka_unit_test_setup_teardown(test_open_for_read_success, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_open_for_read_missing_file, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_open_for_read_nonblock_flag, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_stat_if_unknown_regular_file, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_stat_if_unknown_subdirectory, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_stat_if_unknown_missing_file_returns_false, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_stat_if_unknown_symlink, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_stat_if_unknown_fifo_is_other, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_advise_all_hints, setup_fixture, teardown_fixture),
        cmocka_unit_test_setup_teardown(test_advise_invalid_hint_returns_false, setup_fixture, teardown_fixture),
        cmocka_unit_test(test_advise_invalid_fd_returns_false),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
