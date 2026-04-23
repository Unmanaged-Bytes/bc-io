// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io.h"

#include <stdint.h>
#include <stdlib.h>

static void test_umbrella_exposes_max_path_length(void** state)
{
    (void)state;
    assert_true(BC_IO_MAX_PATH_LENGTH > 0U);
}

static void test_umbrella_exposes_getdents_buffer_size(void** state)
{
    (void)state;
    assert_true(BC_IO_DEFAULT_GETDENTS_BUFFER_SIZE > 0U);
}

static void test_umbrella_exposes_mmap_default_threshold(void** state)
{
    (void)state;
    assert_true(BC_IO_MMAP_DEFAULT_THRESHOLD > 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_umbrella_exposes_max_path_length),
        cmocka_unit_test(test_umbrella_exposes_getdents_buffer_size),
        cmocka_unit_test(test_umbrella_exposes_mmap_default_threshold),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
