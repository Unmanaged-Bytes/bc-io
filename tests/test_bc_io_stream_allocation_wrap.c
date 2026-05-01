// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_stream_internal.h"

#include "bc_allocators.h"
#include "bc_allocators_arena.h"
#include "bc_allocators_pool.h"

#include "bc_core.h"
#include "bc_core_test_wrap.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

BC_TEST_WRAP_FAIL_ON_CALL(bc_allocators_pool_allocate, bool, false,
                          (bc_allocators_context_t* context, size_t size, void** out_pointer),
                          (context, size, out_pointer))

BC_TEST_WRAP_SHOULD_FAIL(bc_allocators_arena_create, bool, false,
                         (bc_allocators_context_t* context, size_t capacity, bc_allocators_arena_t** out_arena),
                         (context, capacity, out_arena))

BC_TEST_WRAP_SHOULD_FAIL(bc_allocators_arena_allocate, bool, false,
                         (bc_allocators_arena_t* arena, size_t size, size_t alignment, void** out_pointer),
                         (arena, size, alignment, out_pointer))

static void reset_mocks(void)
{
    BC_TEST_WRAP_RESET_FAIL_ON_CALL(bc_allocators_pool_allocate);
    BC_TEST_WRAP_RESET_SHOULD_FAIL(bc_allocators_arena_create);
    BC_TEST_WRAP_RESET_SHOULD_FAIL(bc_allocators_arena_allocate);
}

/* ===== Helper: create temporary file ===== */

static char temporary_file_path[256];

static void create_temporary_file(void)
{
    bc_core_copy(temporary_file_path, "/tmp/bc_io_stream_alloc_wrap_XXXXXX", sizeof("/tmp/bc_io_stream_alloc_wrap_XXXXXX"));
    int fd = mkstemp(temporary_file_path);
    assert_true(fd >= 0);
    const char* content = "test data for allocation wrap";
    size_t content_length = 0;
    bc_core_length(content, '\0', &content_length);
    ssize_t written = write(fd, content, content_length);
    assert_true(written > 0);
    close(fd);
}

static void remove_temporary_file(void)
{
    unlink(temporary_file_path);
}

/* ===== Test: pool_allocate fails -> open_file returns false ===== */

static void test_open_pool_allocate_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    bc_allocators_pool_allocate_fail_on_call = bc_allocators_pool_allocate_call_count + 1;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, temporary_file_path, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: arena_create fails -> open_file returns false, pool_free called
 * ===== */

static void test_open_arena_create_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_bc_allocators_arena_create_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, temporary_file_path, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: arena_alloc fails -> open_file returns false, arena destroy +
 * pool free ===== */

static void test_open_arena_alloc_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    mock_bc_allocators_arena_allocate_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file(memory_context, temporary_file_path, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: pool_allocate fails on open_file_descriptor ===== */

static void test_open_fd_pool_allocate_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    int fd = open(temporary_file_path, O_RDONLY | O_CLOEXEC);
    assert_true(fd >= 0);

    bc_allocators_pool_allocate_fail_on_call = bc_allocators_pool_allocate_call_count + 1;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file_descriptor(memory_context, fd, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    close(fd);
    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: arena_create fails on open_file_descriptor ===== */

static void test_open_fd_arena_create_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    int fd = open(temporary_file_path, O_RDONLY | O_CLOEXEC);
    assert_true(fd >= 0);

    mock_bc_allocators_arena_create_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file_descriptor(memory_context, fd, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    close(fd);
    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: arena_alloc fails on open_file_descriptor ===== */

static void test_open_fd_arena_alloc_fails(void** state)
{
    (void)state;
    reset_mocks();
    create_temporary_file();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    int fd = open(temporary_file_path, O_RDONLY | O_CLOEXEC);
    assert_true(fd >= 0);

    mock_bc_allocators_arena_allocate_should_fail = true;

    bc_io_stream_t* stream = NULL;
    bool result = bc_io_stream_open_file_descriptor(memory_context, fd, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, 0, &stream);

    assert_false(result);

    close(fd);
    reset_mocks();
    bc_allocators_context_destroy(memory_context);
    remove_temporary_file();
}

/* ===== Test: pool_allocate fails on open_memory ===== */

static void test_open_memory_pool_allocate_fails(void** state)
{
    (void)state;
    reset_mocks();

    bc_allocators_context_t* memory_context = NULL;
    assert_true(bc_allocators_context_create(NULL, &memory_context));

    const char* data = "test memory data";

    bc_allocators_pool_allocate_fail_on_call = bc_allocators_pool_allocate_call_count + 1;

    bc_io_stream_t* stream = NULL;
    size_t data_length = 0;
    bc_core_length(data, '\0', &data_length);
    bool result = bc_io_stream_open_memory(memory_context, data, data_length, &stream);

    assert_false(result);

    reset_mocks();
    bc_allocators_context_destroy(memory_context);
}

/* ===== main ===== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_open_pool_allocate_fails),        cmocka_unit_test(test_open_arena_create_fails),
        cmocka_unit_test(test_open_arena_alloc_fails),          cmocka_unit_test(test_open_fd_pool_allocate_fails),
        cmocka_unit_test(test_open_fd_arena_create_fails),      cmocka_unit_test(test_open_fd_arena_alloc_fails),
        cmocka_unit_test(test_open_memory_pool_allocate_fails),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
