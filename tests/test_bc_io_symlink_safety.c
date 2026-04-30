// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_file.h"
#include "bc_io_mmap.h"
#include "bc_io_stream.h"

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>

typedef struct symlink_fixture {
    bc_allocators_context_t* memory_context;
    char root_directory[256];
    char regular_file_path[512];
    char symlink_to_regular_path[512];
    char symlink_to_secret_path[512];
    char secret_target_path[512];
} symlink_fixture_t;

static int setup_symlink_fixture(void** state)
{
    symlink_fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);

    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));

    snprintf(fixture->root_directory, sizeof(fixture->root_directory), "/tmp/bc_io_symlink_%d_%ld", (int)getpid(), (long)time(NULL));
    assert_int_equal(mkdir(fixture->root_directory, 0755), 0);

    snprintf(fixture->regular_file_path, sizeof(fixture->regular_file_path), "%s/target.txt", fixture->root_directory);
    int file_descriptor = open(fixture->regular_file_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    const char payload[] = "hello";
    assert_true(write(file_descriptor, payload, sizeof(payload) - 1) == (ssize_t)(sizeof(payload) - 1));
    close(file_descriptor);

    snprintf(fixture->secret_target_path, sizeof(fixture->secret_target_path), "%s/secret.txt", fixture->root_directory);
    int secret_descriptor = open(fixture->secret_target_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert_true(secret_descriptor >= 0);
    const char secret_payload[] = "sensitive";
    assert_true(write(secret_descriptor, secret_payload, sizeof(secret_payload) - 1) == (ssize_t)(sizeof(secret_payload) - 1));
    close(secret_descriptor);

    snprintf(fixture->symlink_to_regular_path, sizeof(fixture->symlink_to_regular_path), "%s/link_to_target", fixture->root_directory);
    assert_int_equal(symlink(fixture->regular_file_path, fixture->symlink_to_regular_path), 0);

    snprintf(fixture->symlink_to_secret_path, sizeof(fixture->symlink_to_secret_path), "%s/link_to_secret", fixture->root_directory);
    assert_int_equal(symlink(fixture->secret_target_path, fixture->symlink_to_secret_path), 0);

    *state = fixture;
    return 0;
}

static int teardown_symlink_fixture(void** state)
{
    symlink_fixture_t* fixture = *state;
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf %s", fixture->root_directory);
    int _sys_rc = system(command);
    (void)_sys_rc;
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

static void test_open_for_read_refuses_symlink(void** state)
{
    const symlink_fixture_t* fixture = *state;
    int file_descriptor = -1;
    bool success = bc_io_file_open_for_read(fixture->symlink_to_regular_path, 0, &file_descriptor);
    assert_false(success);
    assert_int_equal(errno, ELOOP);
}

static void test_open_for_read_opens_regular_file(void** state)
{
    const symlink_fixture_t* fixture = *state;
    int file_descriptor = -1;
    bool success = bc_io_file_open_for_read(fixture->regular_file_path, 0, &file_descriptor);
    assert_true(success);
    assert_true(file_descriptor >= 0);
    close(file_descriptor);
}

static void test_file_open_read_refuses_symlink(void** state)
{
    const symlink_fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.use_noatime = false;
    bc_io_stream_t* stream = NULL;
    bool success = bc_io_file_open_read(fixture->memory_context, fixture->symlink_to_secret_path, &options, &stream);
    assert_false(success);
}

static void test_file_open_auto_refuses_symlink(void** state)
{
    const symlink_fixture_t* fixture = *state;
    bc_io_file_open_options_t options = {0};
    options.use_noatime = false;
    bc_io_file_read_handle_t* handle = NULL;
    bool success = bc_io_file_open_auto(fixture->memory_context, fixture->symlink_to_secret_path, 0U, &options, &handle);
    assert_false(success);
}

static void test_mmap_file_refuses_symlink(void** state)
{
    const symlink_fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    bc_io_mmap_t* map = NULL;
    bool success = bc_io_mmap_file(fixture->memory_context, fixture->symlink_to_regular_path, &options, &map);
    assert_false(success);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_open_for_read_refuses_symlink, setup_symlink_fixture, teardown_symlink_fixture),
        cmocka_unit_test_setup_teardown(test_open_for_read_opens_regular_file, setup_symlink_fixture, teardown_symlink_fixture),
        cmocka_unit_test_setup_teardown(test_file_open_read_refuses_symlink, setup_symlink_fixture, teardown_symlink_fixture),
        cmocka_unit_test_setup_teardown(test_file_open_auto_refuses_symlink, setup_symlink_fixture, teardown_symlink_fixture),
        cmocka_unit_test_setup_teardown(test_mmap_file_refuses_symlink, setup_symlink_fixture, teardown_symlink_fixture),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
