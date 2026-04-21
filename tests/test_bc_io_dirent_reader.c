// SPDX-License-Identifier: MIT

#include "bc_io_dirent_reader.h"

#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmocka.h>

typedef struct dirent_fixture {
    char root_directory[256];
} dirent_fixture_t;

static int setup(void** state)
{
    dirent_fixture_t* fixture = test_calloc(1, sizeof(*fixture));
    snprintf(fixture->root_directory, sizeof(fixture->root_directory), "/tmp/bc-io-dirent-XXXXXX");
    if (mkdtemp(fixture->root_directory) == NULL) {
        test_free(fixture);
        return -1;
    }
    char path[512];
    for (int index = 0; index < 3; index++) {
        snprintf(path, sizeof(path), "%s/file_%d.txt", fixture->root_directory, index);
        int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            return -1;
        }
        close(fd);
    }
    snprintf(path, sizeof(path), "%s/subdir", fixture->root_directory);
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "%s/.hidden", fixture->root_directory);
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    *state = fixture;
    return 0;
}

static int teardown(void** state)
{
    dirent_fixture_t* fixture = *state;
    char path[512];
    for (int index = 0; index < 3; index++) {
        snprintf(path, sizeof(path), "%s/file_%d.txt", fixture->root_directory, index);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/subdir", fixture->root_directory);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/.hidden", fixture->root_directory);
    unlink(path);
    rmdir(fixture->root_directory);
    test_free(fixture);
    return 0;
}

static void test_iterates_all_entries_without_dot_or_dotdot(void** state)
{
    dirent_fixture_t* fixture = *state;
    int dir_fd = open(fixture->root_directory, O_RDONLY | O_DIRECTORY);
    assert_true(dir_fd >= 0);

    bc_io_dirent_reader_t reader;
    bc_io_dirent_reader_init(&reader, dir_fd);

    size_t entry_count = 0;
    bool saw_file_0 = false;
    bool saw_subdir = false;
    bool saw_hidden = false;
    for (;;) {
        bc_io_dirent_entry_t entry;
        bool has_entry = false;
        assert_true(bc_io_dirent_reader_next(&reader, &entry, &has_entry));
        if (!has_entry) {
            break;
        }
        assert_false(strcmp(entry.name, ".") == 0);
        assert_false(strcmp(entry.name, "..") == 0);
        assert_int_equal(entry.name_length, strlen(entry.name));
        entry_count += 1;
        if (strcmp(entry.name, "file_0.txt") == 0) {
            saw_file_0 = true;
            assert_int_equal(entry.d_type, DT_REG);
        } else if (strcmp(entry.name, "subdir") == 0) {
            saw_subdir = true;
            assert_int_equal(entry.d_type, DT_DIR);
        } else if (strcmp(entry.name, ".hidden") == 0) {
            saw_hidden = true;
        }
    }

    assert_int_equal(entry_count, 5u);
    assert_true(saw_file_0);
    assert_true(saw_subdir);
    assert_true(saw_hidden);

    close(dir_fd);
}

static void test_returns_false_after_invalid_fd(void** state)
{
    (void)state;
    bc_io_dirent_reader_t reader;
    bc_io_dirent_reader_init(&reader, -1);
    bc_io_dirent_entry_t entry;
    bool has_entry = true;
    assert_false(bc_io_dirent_reader_next(&reader, &entry, &has_entry));
    assert_int_not_equal(reader.last_errno, 0);
}

static void test_empty_directory_yields_no_entries(void** state)
{
    (void)state;
    char empty_dir[256];
    snprintf(empty_dir, sizeof(empty_dir), "/tmp/bc-io-dirent-empty-XXXXXX");
    assert_non_null(mkdtemp(empty_dir));
    int dir_fd = open(empty_dir, O_RDONLY | O_DIRECTORY);
    assert_true(dir_fd >= 0);

    bc_io_dirent_reader_t reader;
    bc_io_dirent_reader_init(&reader, dir_fd);

    bc_io_dirent_entry_t entry;
    bool has_entry = true;
    assert_true(bc_io_dirent_reader_next(&reader, &entry, &has_entry));
    assert_false(has_entry);

    close(dir_fd);
    rmdir(empty_dir);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_iterates_all_entries_without_dot_or_dotdot, setup, teardown),
        cmocka_unit_test(test_returns_false_after_invalid_fd),
        cmocka_unit_test(test_empty_directory_yields_no_entries),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
