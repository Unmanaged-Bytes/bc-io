// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io_path.h"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static void test_current_directory_succeeds(void** state)
{
    (void)state;
    char buffer[4096];
    size_t length = 0;
    assert_true(bc_io_path_current_directory(buffer, sizeof(buffer), &length));
    assert_true(length > 0);
    assert_int_equal(buffer[0], '/');
    assert_int_equal(buffer[length], '\0');
    assert_int_equal(strlen(buffer), length);
}

static void test_current_directory_buffer_too_small(void** state)
{
    (void)state;
    char buffer[2];
    size_t length = 999;
    assert_false(bc_io_path_current_directory(buffer, sizeof(buffer), &length));
}

static void test_current_directory_after_chdir(void** state)
{
    (void)state;
    char saved_buffer[4096];
    assert_non_null(getcwd(saved_buffer, sizeof(saved_buffer)));

    assert_int_equal(chdir("/tmp"), 0);

    char buffer[4096];
    size_t length = 0;
    assert_true(bc_io_path_current_directory(buffer, sizeof(buffer), &length));
    assert_int_equal(length, 4);
    assert_memory_equal(buffer, "/tmp", 4);
    assert_int_equal(buffer[length], '\0');

    assert_int_equal(chdir(saved_buffer), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_current_directory_succeeds),
        cmocka_unit_test(test_current_directory_buffer_too_small),
        cmocka_unit_test(test_current_directory_after_chdir),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
