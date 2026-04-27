// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io_random.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define BC_IO_RANDOM_TEST_THREAD_COUNT 16
#define BC_IO_RANDOM_TEST_PER_THREAD_CALLS 256

static void test_random_bytes_zero_length(void** state)
{
    (void)state;
    bool ok = bc_io_random_bytes(NULL, 0);
    assert_true(ok);
}

static void test_random_bytes_null_with_nonzero_length(void** state)
{
    (void)state;
    bool ok = bc_io_random_bytes(NULL, 8);
    assert_false(ok);
}

static void test_random_bytes_fills_buffer(void** state)
{
    (void)state;
    unsigned char buffer[64];
    memset(buffer, 0xAA, sizeof(buffer));
    bool ok = bc_io_random_bytes(buffer, sizeof(buffer));
    assert_true(ok);

    bool all_same = true;
    for (size_t index = 1; index < sizeof(buffer); ++index) {
        if (buffer[index] != buffer[0]) {
            all_same = false;
            break;
        }
    }
    assert_false(all_same);
}

static void test_random_bytes_large_buffer(void** state)
{
    (void)state;
    unsigned char buffer[4096];
    bool ok = bc_io_random_bytes(buffer, sizeof(buffer));
    assert_true(ok);
}

static void test_random_unsigned_integer_64_basic(void** state)
{
    (void)state;
    uint64_t value_one = 0;
    uint64_t value_two = 0;
    assert_true(bc_io_random_unsigned_integer_64(&value_one));
    assert_true(bc_io_random_unsigned_integer_64(&value_two));
    assert_int_not_equal(value_one, value_two);
}

static void test_random_unsigned_integer_64_null(void** state)
{
    (void)state;
    bool ok = bc_io_random_unsigned_integer_64(NULL);
    assert_false(ok);
}

typedef struct {
    int success_count;
    int failure_count;
    uint64_t accumulator;
} bc_io_random_thread_result_t;

static void* bc_io_random_thread_worker(void* argument)
{
    bc_io_random_thread_result_t* result = (bc_io_random_thread_result_t*)argument;
    for (int call_index = 0; call_index < BC_IO_RANDOM_TEST_PER_THREAD_CALLS; ++call_index) {
        uint64_t value = 0;
        if (bc_io_random_unsigned_integer_64(&value)) {
            result->success_count += 1;
            result->accumulator ^= value;
        } else {
            result->failure_count += 1;
        }
    }
    return NULL;
}

static void test_random_concurrent_calls_no_contention(void** state)
{
    (void)state;
    pthread_t threads[BC_IO_RANDOM_TEST_THREAD_COUNT];
    bc_io_random_thread_result_t results[BC_IO_RANDOM_TEST_THREAD_COUNT];
    memset(results, 0, sizeof(results));

    for (int thread_index = 0; thread_index < BC_IO_RANDOM_TEST_THREAD_COUNT; ++thread_index) {
        int rc = pthread_create(&threads[thread_index], NULL, bc_io_random_thread_worker, &results[thread_index]);
        assert_int_equal(rc, 0);
    }
    for (int thread_index = 0; thread_index < BC_IO_RANDOM_TEST_THREAD_COUNT; ++thread_index) {
        int rc = pthread_join(threads[thread_index], NULL);
        assert_int_equal(rc, 0);
    }

    for (int thread_index = 0; thread_index < BC_IO_RANDOM_TEST_THREAD_COUNT; ++thread_index) {
        assert_int_equal(results[thread_index].success_count, BC_IO_RANDOM_TEST_PER_THREAD_CALLS);
        assert_int_equal(results[thread_index].failure_count, 0);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_random_bytes_zero_length),
        cmocka_unit_test(test_random_bytes_null_with_nonzero_length),
        cmocka_unit_test(test_random_bytes_fills_buffer),
        cmocka_unit_test(test_random_bytes_large_buffer),
        cmocka_unit_test(test_random_unsigned_integer_64_basic),
        cmocka_unit_test(test_random_unsigned_integer_64_null),
        cmocka_unit_test(test_random_concurrent_calls_no_contention),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
