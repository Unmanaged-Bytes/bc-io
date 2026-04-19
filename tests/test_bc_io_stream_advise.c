// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_io_stream.h"

#include "bc_allocators.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fixture {
    bc_allocators_context_t* memory_context;
    char path[64];
    int fd;
};

static int fixture_setup(void** state)
{
    struct fixture* f = calloc(1, sizeof(*f));
    assert_non_null(f);
    assert_true(bc_allocators_context_create(NULL, &f->memory_context));

    strcpy(f->path, "/tmp/bc_io_stream_advise_XXXXXX");
    f->fd = mkstemp(f->path);
    assert_true(f->fd >= 0);
    const char* data = "hello advise test";
    ssize_t written = write(f->fd, data, strlen(data));
    (void)written;
    close(f->fd);
    f->fd = -1;

    *state = f;
    return 0;
}

static int fixture_teardown(void** state)
{
    struct fixture* f = *state;
    unlink(f->path);
    bc_allocators_context_destroy(f->memory_context);
    free(f);
    return 0;
}

static void test_advise_sequential_on_file(void** state)
{
    struct fixture* f = *state;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file(f->memory_context, f->path, BC_IO_STREAM_MODE_READ, 0, &stream));
    assert_true(bc_io_stream_advise(stream, BC_IO_STREAM_ACCESS_SEQUENTIAL));
    bc_io_stream_close(stream);
}

static void test_advise_random_on_file(void** state)
{
    struct fixture* f = *state;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file(f->memory_context, f->path, BC_IO_STREAM_MODE_READ, 0, &stream));
    assert_true(bc_io_stream_advise(stream, BC_IO_STREAM_ACCESS_RANDOM));
    bc_io_stream_close(stream);
}

static void test_advise_drop_on_file(void** state)
{
    struct fixture* f = *state;
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_file(f->memory_context, f->path, BC_IO_STREAM_MODE_READ, 0, &stream));
    assert_true(bc_io_stream_advise(stream, BC_IO_STREAM_ACCESS_DROP));
    bc_io_stream_close(stream);
}

static void test_advise_on_memory_stream_is_noop(void** state)
{
    struct fixture* f = *state;
    const char data[] = "memory stream";
    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_stream_open_memory(f->memory_context, data, sizeof(data), &stream));
    assert_true(bc_io_stream_advise(stream, BC_IO_STREAM_ACCESS_SEQUENTIAL));
    bc_io_stream_close(stream);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_advise_sequential_on_file, fixture_setup, fixture_teardown),
        cmocka_unit_test_setup_teardown(test_advise_random_on_file, fixture_setup, fixture_teardown),
        cmocka_unit_test_setup_teardown(test_advise_drop_on_file, fixture_setup, fixture_teardown),
        cmocka_unit_test_setup_teardown(test_advise_on_memory_stream_is_noop, fixture_setup, fixture_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
