// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#include "bc_io_file_inode.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ===== Mocks ===== */

static int g_pool_allocate_call_count = 0;
static int g_pool_allocate_fail_on_call = -1;

static int g_safe_multiply_call_count = 0;
static int g_safe_multiply_fail_on_call = -1;

static int g_safe_add_call_count = 0;
static int g_safe_add_fail_on_call = -1;

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr);

bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr)
{
    g_pool_allocate_call_count++;
    if (g_pool_allocate_call_count == g_pool_allocate_fail_on_call) {
        *out_ptr = NULL;
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out_ptr);
}

bool __real_bc_core_safe_multiply(size_t a, size_t b, size_t* out_result);

bool __wrap_bc_core_safe_multiply(size_t a, size_t b, size_t* out_result)
{
    g_safe_multiply_call_count++;
    if (g_safe_multiply_call_count == g_safe_multiply_fail_on_call) {
        return false;
    }
    return __real_bc_core_safe_multiply(a, b, out_result);
}

bool __real_bc_core_safe_add(size_t a, size_t b, size_t* out_result);

bool __wrap_bc_core_safe_add(size_t a, size_t b, size_t* out_result)
{
    g_safe_add_call_count++;
    if (g_safe_add_call_count == g_safe_add_fail_on_call) {
        return false;
    }
    return __real_bc_core_safe_add(a, b, out_result);
}

static void reset_mocks(void)
{
    g_pool_allocate_call_count = 0;
    g_pool_allocate_fail_on_call = -1;
    g_safe_multiply_call_count = 0;
    g_safe_multiply_fail_on_call = -1;
    g_safe_add_call_count = 0;
    g_safe_add_fail_on_call = -1;
}

static bc_allocators_context_t* create_memory(void)
{
    bc_allocators_context_t* memory = NULL;
    bc_allocators_context_create(NULL, &memory);
    return memory;
}

/* Exercises the value < 2 branch of the internal round_up_to_power_of_two.
   With initial_capacity = 1 the helper returns 2 rather than looping. */
static void test_inode_set_create_capacity_one_rounds_up(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    bc_io_file_inode_set_t* set = NULL;
    assert_true(bc_io_file_inode_set_create(memory, 1, &set));
    assert_non_null(set);

    bc_io_file_inode_set_destroy(set);
    bc_allocators_context_destroy(memory);
}

static void test_inode_set_create_set_pool_alloc_fail(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    g_pool_allocate_fail_on_call = 1;
    bc_io_file_inode_set_t* set = NULL;
    assert_false(bc_io_file_inode_set_create(memory, 16, &set));

    bc_allocators_context_destroy(memory);
}

static void test_inode_set_create_buckets_safe_multiply_overflow(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    /* First safe_multiply call is inside allocate_buckets: fail it to cover
       the overflow branch. */
    g_safe_multiply_fail_on_call = 1;
    bc_io_file_inode_set_t* set = NULL;
    assert_false(bc_io_file_inode_set_create(memory, 16, &set));

    bc_allocators_context_destroy(memory);
}

static void test_inode_set_create_buckets_safe_add_overflow(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    /* safe_multiply succeeds, safe_add fails in allocate_buckets. */
    g_safe_add_fail_on_call = 1;
    bc_io_file_inode_set_t* set = NULL;
    assert_false(bc_io_file_inode_set_create(memory, 16, &set));

    bc_allocators_context_destroy(memory);
}

static void test_inode_set_create_buckets_pool_alloc_fail(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    /* First pool_allocate is for the set struct. Second is for the buckets
       inside allocate_buckets. Fail that one. */
    g_pool_allocate_fail_on_call = 2;
    bc_io_file_inode_set_t* set = NULL;
    assert_false(bc_io_file_inode_set_create(memory, 16, &set));

    bc_allocators_context_destroy(memory);
}

/* When insert crosses the load factor threshold, grow_locked runs
   allocate_buckets. Forcing pool_allocate to fail there must produce
   a clean insert failure and leave the set unchanged. */
static void test_inode_set_insert_grow_pool_alloc_fail(void** state)
{
    (void)state;
    reset_mocks();
    bc_allocators_context_t* memory = create_memory();

    bc_io_file_inode_set_t* set = NULL;
    assert_true(bc_io_file_inode_set_create(memory, 2, &set));

    /* Initial capacity rounds up to 2 (two slots). Load factor grows at
       (size + 1) * 4 > capacity * 3  ->  first insert is allowed, second
       insert crosses the threshold and triggers grow_locked. */
    bool was_present = false;
    assert_true(bc_io_file_inode_set_insert(set, 10, 100, &was_present));

    /* Fail the next pool_allocate, which happens inside allocate_buckets
       during grow_locked. */
    g_pool_allocate_fail_on_call = g_pool_allocate_call_count + 1;
    assert_false(bc_io_file_inode_set_insert(set, 20, 200, &was_present));

    /* The existing entry must still be there. */
    reset_mocks();
    bool contains = false;
    assert_true(bc_io_file_inode_set_contains(set, 10, 100, &contains));
    assert_true(contains);

    bc_io_file_inode_set_destroy(set);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_inode_set_create_capacity_one_rounds_up),
        cmocka_unit_test(test_inode_set_create_set_pool_alloc_fail),
        cmocka_unit_test(test_inode_set_create_buckets_safe_multiply_overflow),
        cmocka_unit_test(test_inode_set_create_buckets_safe_add_overflow),
        cmocka_unit_test(test_inode_set_create_buckets_pool_alloc_fail),
        cmocka_unit_test(test_inode_set_insert_grow_pool_alloc_fail),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
