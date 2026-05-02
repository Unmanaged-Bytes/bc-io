// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io_perm.h"

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_format_mode_human_regular_file_755(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_int_equal((int)length, 10);
    assert_memory_equal(buffer, "-rwxr-xr-x", 10);
}

static void test_format_mode_human_directory_700(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "drwx------", 10);
}

static void test_format_mode_human_symlink_777(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFLNK | 0777u;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "lrwxrwxrwx", 10);
}

static void test_format_mode_human_setuid_with_exec(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFREG | S_ISUID | S_IRUSR | S_IXUSR | S_IRGRP | S_IROTH;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "-r-sr--r--", 10);
}

static void test_format_mode_human_setuid_without_exec(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFREG | S_ISUID | S_IRUSR | S_IRGRP | S_IROTH;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "-r-Sr--r--", 10);
}

static void test_format_mode_human_setgid_with_exec(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFREG | S_ISGID | S_IRUSR | S_IRGRP | S_IXGRP | S_IROTH;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "-r--r-sr--", 10);
}

static void test_format_mode_human_sticky_with_exec(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFDIR | S_ISVTX | 0777u;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "drwxrwxrwt", 10);
}

static void test_format_mode_human_sticky_without_exec(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    const uint32_t mode = S_IFDIR | S_ISVTX | 0770u;
    assert_true(bc_io_perm_format_mode_human(mode, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "drwxrwx--T", 10);
}

static void test_format_mode_human_buffer_too_small(void** state)
{
    (void)state;
    char buffer[5];
    size_t length = 0;
    assert_false(bc_io_perm_format_mode_human(S_IFREG | 0644u, buffer, sizeof(buffer), &length));
}

static void test_format_mode_human_special_types(void** state)
{
    (void)state;
    char buffer[16];
    size_t length = 0;
    assert_true(bc_io_perm_format_mode_human(S_IFCHR | 0644u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)buffer[0], 'c');
    assert_true(bc_io_perm_format_mode_human(S_IFBLK | 0644u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)buffer[0], 'b');
    assert_true(bc_io_perm_format_mode_human(S_IFIFO | 0644u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)buffer[0], 'p');
    assert_true(bc_io_perm_format_mode_human(S_IFSOCK | 0644u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)buffer[0], 's');
}

static void test_parse_mode_human_round_trip_755(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_human("-rwxr-xr-x", 10u, &mode));
    assert_true((mode & 0777u) == 0755u);
}

static void test_parse_mode_human_without_type_byte(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_human("rw-r--r--", 9u, &mode));
    assert_true((mode & 0777u) == 0644u);
}

static void test_parse_mode_human_with_setuid(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_human("-r-sr--r--", 10u, &mode));
    assert_true((mode & S_ISUID) != 0);
    assert_true((mode & S_IXUSR) != 0);
}

static void test_parse_mode_human_with_sticky_uppercase(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_human("drwxrwx--T", 10u, &mode));
    assert_true((mode & S_ISVTX) != 0);
    assert_true((mode & S_IXOTH) == 0);
}

static void test_parse_mode_human_invalid_length(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_false(bc_io_perm_parse_mode_human("rwx", 3u, &mode));
    assert_false(bc_io_perm_parse_mode_human("rwxrwxrwxrwx", 12u, &mode));
}

static void test_parse_mode_human_invalid_chars(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_false(bc_io_perm_parse_mode_human("xrwxr-xr-x", 10u, &mode));
    assert_false(bc_io_perm_parse_mode_human("-zwxr-xr-x", 10u, &mode));
    assert_false(bc_io_perm_parse_mode_human("-r?xr-xr-x", 10u, &mode));
}

static void test_format_mode_octal_simple(void** state)
{
    (void)state;
    char buffer[8];
    size_t length = 0;
    assert_true(bc_io_perm_format_mode_octal(0644u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)length, 4);
    assert_memory_equal(buffer, "0644", 4);

    assert_true(bc_io_perm_format_mode_octal(0755u, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "0755", 4);
}

static void test_format_mode_octal_with_setuid(void** state)
{
    (void)state;
    char buffer[8];
    size_t length = 0;
    assert_true(bc_io_perm_format_mode_octal(S_ISUID | 0755u, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "4755", 4);
}

static void test_format_mode_octal_with_sticky(void** state)
{
    (void)state;
    char buffer[8];
    size_t length = 0;
    assert_true(bc_io_perm_format_mode_octal(S_ISVTX | 0777u, buffer, sizeof(buffer), &length));
    assert_memory_equal(buffer, "1777", 4);
}

static void test_format_mode_octal_buffer_too_small(void** state)
{
    (void)state;
    char buffer[3];
    size_t length = 0;
    assert_false(bc_io_perm_format_mode_octal(0644u, buffer, sizeof(buffer), &length));
}

static void test_parse_mode_octal_three_digits(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_octal("755", 3u, &mode));
    assert_true(mode == 0755u);
}

static void test_parse_mode_octal_four_digits_with_special(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_true(bc_io_perm_parse_mode_octal("4755", 4u, &mode));
    assert_true((mode & S_ISUID) != 0);
    assert_true((mode & 0777u) == 0755u);

    assert_true(bc_io_perm_parse_mode_octal("1777", 4u, &mode));
    assert_true((mode & S_ISVTX) != 0);
}

static void test_parse_mode_octal_invalid(void** state)
{
    (void)state;
    uint32_t mode = 0;
    assert_false(bc_io_perm_parse_mode_octal("", 0u, &mode));
    assert_false(bc_io_perm_parse_mode_octal("89", 2u, &mode));
    assert_false(bc_io_perm_parse_mode_octal("12345", 5u, &mode));
    assert_false(bc_io_perm_parse_mode_octal("a", 1u, &mode));
}

static void test_resolve_user_name_root(void** state)
{
    (void)state;
    char buffer[64];
    size_t length = 0;
    assert_true(bc_io_perm_resolve_user_name(0u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)length, 4);
    assert_memory_equal(buffer, "root", 4);
}

static void test_resolve_user_name_invalid_uid(void** state)
{
    (void)state;
    char buffer[64];
    size_t length = 0;
    assert_false(bc_io_perm_resolve_user_name(99999999u, buffer, sizeof(buffer), &length));
}

static void test_resolve_user_name_buffer_too_small(void** state)
{
    (void)state;
    char buffer[2];
    size_t length = 0;
    assert_false(bc_io_perm_resolve_user_name(0u, buffer, sizeof(buffer), &length));
}

static void test_resolve_group_name_root(void** state)
{
    (void)state;
    char buffer[64];
    size_t length = 0;
    assert_true(bc_io_perm_resolve_group_name(0u, buffer, sizeof(buffer), &length));
    assert_int_equal((int)length, 4);
    assert_memory_equal(buffer, "root", 4);
}

static void test_resolve_group_name_invalid_gid(void** state)
{
    (void)state;
    char buffer[64];
    size_t length = 0;
    assert_false(bc_io_perm_resolve_group_name(99999999u, buffer, sizeof(buffer), &length));
}

static void test_resolve_user_id_root(void** state)
{
    (void)state;
    uint32_t user_id = 9999u;
    assert_true(bc_io_perm_resolve_user_id("root", 4u, &user_id));
    assert_int_equal((int)user_id, 0);
}

static void test_resolve_user_id_unknown(void** state)
{
    (void)state;
    uint32_t user_id = 0;
    assert_false(bc_io_perm_resolve_user_id("definitely_not_a_user_xyz", 25u, &user_id));
}

static void test_resolve_group_id_root(void** state)
{
    (void)state;
    uint32_t group_id = 9999u;
    assert_true(bc_io_perm_resolve_group_id("root", 4u, &group_id));
    assert_int_equal((int)group_id, 0);
}

static void test_resolve_user_id_name_too_long(void** state)
{
    (void)state;
    char long_name[300];
    for (size_t index = 0; index < sizeof(long_name); index++) {
        long_name[index] = 'x';
    }
    uint32_t user_id = 0;
    assert_false(bc_io_perm_resolve_user_id(long_name, sizeof(long_name), &user_id));
}

static void test_format_then_parse_octal_round_trip(void** state)
{
    (void)state;
    const uint32_t test_modes[] = {0644u, 0755u, 0777u, 0700u, 0000u, S_ISUID | 0755u, S_ISGID | 0644u, S_ISVTX | 0777u};
    for (size_t index = 0; index < sizeof(test_modes) / sizeof(test_modes[0]); index++) {
        char buffer[8];
        size_t length = 0;
        assert_true(bc_io_perm_format_mode_octal(test_modes[index], buffer, sizeof(buffer), &length));
        uint32_t parsed = 0;
        assert_true(bc_io_perm_parse_mode_octal(buffer, length, &parsed));
        assert_int_equal((int)(parsed & 07777u), (int)(test_modes[index] & 07777u));
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_format_mode_human_regular_file_755),
        cmocka_unit_test(test_format_mode_human_directory_700),
        cmocka_unit_test(test_format_mode_human_symlink_777),
        cmocka_unit_test(test_format_mode_human_setuid_with_exec),
        cmocka_unit_test(test_format_mode_human_setuid_without_exec),
        cmocka_unit_test(test_format_mode_human_setgid_with_exec),
        cmocka_unit_test(test_format_mode_human_sticky_with_exec),
        cmocka_unit_test(test_format_mode_human_sticky_without_exec),
        cmocka_unit_test(test_format_mode_human_buffer_too_small),
        cmocka_unit_test(test_format_mode_human_special_types),
        cmocka_unit_test(test_parse_mode_human_round_trip_755),
        cmocka_unit_test(test_parse_mode_human_without_type_byte),
        cmocka_unit_test(test_parse_mode_human_with_setuid),
        cmocka_unit_test(test_parse_mode_human_with_sticky_uppercase),
        cmocka_unit_test(test_parse_mode_human_invalid_length),
        cmocka_unit_test(test_parse_mode_human_invalid_chars),
        cmocka_unit_test(test_format_mode_octal_simple),
        cmocka_unit_test(test_format_mode_octal_with_setuid),
        cmocka_unit_test(test_format_mode_octal_with_sticky),
        cmocka_unit_test(test_format_mode_octal_buffer_too_small),
        cmocka_unit_test(test_parse_mode_octal_three_digits),
        cmocka_unit_test(test_parse_mode_octal_four_digits_with_special),
        cmocka_unit_test(test_parse_mode_octal_invalid),
        cmocka_unit_test(test_resolve_user_name_root),
        cmocka_unit_test(test_resolve_user_name_invalid_uid),
        cmocka_unit_test(test_resolve_user_name_buffer_too_small),
        cmocka_unit_test(test_resolve_group_name_root),
        cmocka_unit_test(test_resolve_group_name_invalid_gid),
        cmocka_unit_test(test_resolve_user_id_root),
        cmocka_unit_test(test_resolve_user_id_unknown),
        cmocka_unit_test(test_resolve_group_id_root),
        cmocka_unit_test(test_resolve_user_id_name_too_long),
        cmocka_unit_test(test_format_then_parse_octal_round_trip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
