// SPDX-License-Identifier: MIT

#include "bc_core.h"
#include "bc_io_file.h"
#include "bc_io_file_inode.h"
#include "bc_allocators.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <cmocka.h>

typedef struct fixture {
    bc_allocators_context_t* memory_context;
    bc_io_file_inode_set_t* inode_set;
} fixture_t;

static int setup_default(void** state)
{
    fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);
    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));
    assert_true(bc_io_file_inode_set_create(fixture->memory_context, 16, &fixture->inode_set));
    *state = fixture;
    return 0;
}

static int teardown_default(void** state)
{
    fixture_t* fixture = *state;
    bc_io_file_inode_set_destroy(fixture->inode_set);
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

/* ========== create ========== */

static void test_create_capacity_zero_uses_default(void** state)
{
    (void)state;
    bc_allocators_context_t* memory_context = NULL;
    bc_io_file_inode_set_t* inode_set = NULL;
    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &memory_context));
    assert_true(bc_io_file_inode_set_create(memory_context, 0, &inode_set));
    assert_non_null(inode_set);
    bc_io_file_inode_set_destroy(inode_set);
    bc_allocators_context_destroy(memory_context);
}

/* ========== insert / contains ========== */

static void test_insert_new_returns_not_present(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = true;
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, 100, &was_already_present));
    assert_false(was_already_present);

    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, 1);
}

static void test_insert_duplicate_returns_present(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, 100, &was_already_present));
    assert_false(was_already_present);

    was_already_present = false;
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, 100, &was_already_present));
    assert_true(was_already_present);

    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, 1);
}

static void test_contains_after_insert(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 42, 7, &was_already_present));

    bool is_present = false;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 42, 7, &is_present));
    assert_true(is_present);

    is_present = true;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 43, 7, &is_present));
    assert_false(is_present);
}

/* ========== clear ========== */

static void test_clear_resets_size_preserves_capacity(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;
    for (size_t index = 0; index < 5; ++index) {
        assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, (ino_t)index, &was_already_present));
    }

    assert_true(bc_io_file_inode_set_clear(fixture->inode_set));

    size_t size = 999;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, 0);

    was_already_present = true;
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, 0, &was_already_present));
    assert_false(was_already_present);
}

/* ========== get_size ========== */

static void test_get_size_matches_distinct_inserts(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;
    const size_t element_count = 37;
    for (size_t index = 0; index < element_count; ++index) {
        assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 2, (ino_t)index, &was_already_present));
    }
    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, element_count);
}

/* ========== grow ========== */

static void test_grow_to_ten_thousand(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;
    const size_t total = 10000;
    for (size_t index = 0; index < total; ++index) {
        assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 0, (ino_t)index, &was_already_present));
        assert_false(was_already_present);
    }

    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, total);

    for (size_t index = 0; index < total; ++index) {
        bool is_present = false;
        assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 0, (ino_t)index, &is_present));
        assert_true(is_present);
    }
}

/* ========== collision linear probing ========== */

static void test_collision_linear_probing(void** state)
{
    fixture_t* fixture = *state;
    bool was_already_present = false;

    /* Two different keys (dev, ino) are inserted. The concrete hash is
   * implementation-defined, but both must be reachable via contains,
   * even if they collide on the same bucket. We use values that could
   * plausibly collide in any open-addressing scheme. */
    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 0, 0, &was_already_present));
    assert_false(was_already_present);

    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 1, 0, &was_already_present));
    assert_false(was_already_present);

    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 0, 16, &was_already_present));
    assert_false(was_already_present);

    assert_true(bc_io_file_inode_set_insert(fixture->inode_set, 16, 0, &was_already_present));
    assert_false(was_already_present);

    bool is_present = false;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 0, 0, &is_present));
    assert_true(is_present);

    is_present = false;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 1, 0, &is_present));
    assert_true(is_present);

    is_present = false;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 0, 16, &is_present));
    assert_true(is_present);

    is_present = false;
    assert_true(bc_io_file_inode_set_contains(fixture->inode_set, 16, 0, &is_present));
    assert_true(is_present);

    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, 4);
}

static void test_stress_large_insert(void** state)
{
    fixture_t* fixture = *state;
    const size_t device_count = 16;
    const size_t per_device = 1000;

    for (size_t device_index = 0; device_index < device_count; ++device_index) {
        for (size_t inode_index = 0; inode_index < per_device; ++inode_index) {
            bool was_already_present = false;
            assert_true(
                bc_io_file_inode_set_insert(fixture->inode_set, (dev_t)(device_index + 1), (ino_t)inode_index, &was_already_present));
            assert_false(was_already_present);
        }
    }

    size_t size = 0;
    assert_true(bc_io_file_inode_set_get_size(fixture->inode_set, &size));
    assert_int_equal(size, device_count * per_device);

    for (size_t device_index = 0; device_index < device_count; ++device_index) {
        for (size_t inode_index = 0; inode_index < per_device; ++inode_index) {
            bool is_present = false;
            assert_true(bc_io_file_inode_set_contains(fixture->inode_set, (dev_t)(device_index + 1), (ino_t)inode_index, &is_present));
            assert_true(is_present);
        }
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_capacity_zero_uses_default),
        cmocka_unit_test_setup_teardown(test_insert_new_returns_not_present, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_insert_duplicate_returns_present, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_contains_after_insert, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_clear_resets_size_preserves_capacity, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_get_size_matches_distinct_inserts, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_grow_to_ten_thousand, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_collision_linear_probing, setup_default, teardown_default),
        cmocka_unit_test_setup_teardown(test_stress_large_insert, setup_default, teardown_default),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
