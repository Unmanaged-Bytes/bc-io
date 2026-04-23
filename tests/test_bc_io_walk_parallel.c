// SPDX-License-Identifier: MIT

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "bc_allocators.h"
#include "bc_concurrency.h"
#include "bc_io_walk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct walk_accumulator {
    char paths[64][4096];
    size_t path_lengths[64];
    size_t count;
    atomic_int lock;
} walk_accumulator_t;

typedef struct walk_test_fixture {
    bc_allocators_context_t* memory_context;
    bc_concurrency_context_t* concurrency_context;
    char root[128];
    walk_accumulator_t accumulator;
} walk_test_fixture_t;

static void accumulator_lock(walk_accumulator_t* acc)
{
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&acc->lock, &expected, 1, memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
    }
}

static void accumulator_unlock(walk_accumulator_t* acc)
{
    atomic_store_explicit(&acc->lock, 0, memory_order_release);
}

static bool visit_accumulate_files(const bc_io_walk_entry_t* entry, void* user_data)
{
    walk_accumulator_t* acc = (walk_accumulator_t*)user_data;
    if (entry->kind != BC_IO_WALK_ENTRY_FILE) {
        return true;
    }
    accumulator_lock(acc);
    if (acc->count < 64) {
        memcpy(acc->paths[acc->count], entry->absolute_path, entry->absolute_path_length);
        acc->paths[acc->count][entry->absolute_path_length] = '\0';
        acc->path_lengths[acc->count] = entry->absolute_path_length;
        acc->count++;
    }
    accumulator_unlock(acc);
    return true;
}

/* cppcheck-suppress constParameterCallback; signature fixed by bc_io_walk_filter_fn */
static bool filter_reject_directory_by_name(const bc_io_walk_entry_t* entry, void* user_data)
{
    const char* rejected_name = (const char*)user_data;
    if (entry->kind != BC_IO_WALK_ENTRY_DIRECTORY) {
        return true;
    }
    size_t name_length = strlen(rejected_name);
    if (entry->absolute_path_length < name_length) {
        return true;
    }
    return memcmp(entry->absolute_path + entry->absolute_path_length - name_length, rejected_name, name_length) != 0;
}

static int build_tree_with_files(const char* root_path, size_t file_count)
{
    if (mkdir(root_path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    for (size_t i = 0; i < file_count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/file_%zu.txt", root_path, i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return -1;
        }
        const char* payload = "hello";
        if (write(fd, payload, 5) != 5) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static void cleanup_tree_recursive(const char* root_path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root_path);
    (void)!system(cmd);
}

static int setup(void** state)
{
    walk_test_fixture_t* fixture = test_calloc(1, sizeof(*fixture));

    bc_allocators_context_config_t config = {.tracking_enabled = false};
    if (!bc_allocators_context_create(&config, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }

    bc_concurrency_config_t concurrency_config = {.worker_count = 4};
    if (!bc_concurrency_create(fixture->memory_context, &concurrency_config, &fixture->concurrency_context)) {
        bc_allocators_context_destroy(fixture->memory_context);
        test_free(fixture);
        return -1;
    }

    snprintf(fixture->root, sizeof(fixture->root), "/tmp/bc_io_walk_test_%d", getpid());
    cleanup_tree_recursive(fixture->root);

    atomic_store_explicit(&fixture->accumulator.lock, 0, memory_order_relaxed);
    fixture->accumulator.count = 0;

    *state = fixture;
    return 0;
}

static int teardown(void** state)
{
    walk_test_fixture_t* fixture = *state;
    cleanup_tree_recursive(fixture->root);
    bc_concurrency_destroy(fixture->concurrency_context);
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static void test_walk_rejects_invalid_config(void** state)
{
    walk_test_fixture_t* fixture = *state;

    bc_io_walk_config_t bad = {
        .root = NULL,
        .root_length = 0,
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    assert_false(bc_io_walk_parallel(&bad, NULL));

    bad.root = "/tmp";
    bad.root_length = 4;
    bad.visit = NULL;
    assert_false(bc_io_walk_parallel(&bad, NULL));
}

static void test_walk_empty_directory(void** state)
{
    walk_test_fixture_t* fixture = *state;
    assert_int_equal(mkdir(fixture->root, 0755), 0);

    bc_io_walk_config_t config = {
        .root = fixture->root,
        .root_length = strlen(fixture->root),
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    bc_io_walk_stats_t stats;
    assert_true(bc_io_walk_parallel(&config, &stats));

    assert_int_equal(fixture->accumulator.count, 0);
    assert_int_equal(stats.files_visited, 0);
    assert_int_equal(stats.directories_visited, 1);
    assert_int_equal(stats.errors_encountered, 0);
}

static void test_walk_flat_files(void** state)
{
    walk_test_fixture_t* fixture = *state;
    assert_int_equal(build_tree_with_files(fixture->root, 10), 0);

    bc_io_walk_config_t config = {
        .root = fixture->root,
        .root_length = strlen(fixture->root),
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    bc_io_walk_stats_t stats;
    assert_true(bc_io_walk_parallel(&config, &stats));

    assert_int_equal(fixture->accumulator.count, 10);
    assert_int_equal(stats.files_visited, 10);
    assert_int_equal(stats.directories_visited, 1);
}

static void test_walk_nested_files(void** state)
{
    walk_test_fixture_t* fixture = *state;
    char child_dir[256];

    assert_int_equal(mkdir(fixture->root, 0755), 0);
    snprintf(child_dir, sizeof(child_dir), "%s/sub", fixture->root);
    assert_int_equal(build_tree_with_files(child_dir, 5), 0);
    assert_int_equal(build_tree_with_files(fixture->root, 3), 0);

    bc_io_walk_config_t config = {
        .root = fixture->root,
        .root_length = strlen(fixture->root),
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    bc_io_walk_stats_t stats;
    assert_true(bc_io_walk_parallel(&config, &stats));

    assert_int_equal(fixture->accumulator.count, 8);
    assert_int_equal(stats.files_visited, 8);
    assert_int_equal(stats.directories_visited, 2);
}

static void test_walk_filter_rejects_directory(void** state)
{
    walk_test_fixture_t* fixture = *state;
    char keep_dir[256];
    char reject_dir[256];

    assert_int_equal(mkdir(fixture->root, 0755), 0);
    snprintf(keep_dir, sizeof(keep_dir), "%s/keep", fixture->root);
    snprintf(reject_dir, sizeof(reject_dir), "%s/reject", fixture->root);
    assert_int_equal(build_tree_with_files(keep_dir, 3), 0);
    assert_int_equal(build_tree_with_files(reject_dir, 4), 0);

    bc_io_walk_config_t config = {
        .root = fixture->root,
        .root_length = strlen(fixture->root),
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .filter = filter_reject_directory_by_name,
        .filter_user_data = (void*)"/reject",
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    bc_io_walk_stats_t stats;
    assert_true(bc_io_walk_parallel(&config, &stats));

    assert_int_equal(fixture->accumulator.count, 3);
    assert_int_equal(stats.directories_visited, 2);
    assert_int_equal(stats.files_skipped, 1);
}

static void test_walk_nonexistent_root_reports_error(void** state)
{
    walk_test_fixture_t* fixture = *state;

    bc_io_walk_config_t config = {
        .root = fixture->root,
        .root_length = strlen(fixture->root),
        .main_memory_context = fixture->memory_context,
        .concurrency_context = fixture->concurrency_context,
        .visit = visit_accumulate_files,
        .visit_user_data = &fixture->accumulator,
    };
    bc_io_walk_stats_t stats;
    assert_true(bc_io_walk_parallel(&config, &stats));

    assert_int_equal(stats.errors_encountered, 1);
    assert_int_equal(stats.directories_visited, 0);
    assert_int_equal(fixture->accumulator.count, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_walk_rejects_invalid_config, setup, teardown),
        cmocka_unit_test_setup_teardown(test_walk_empty_directory, setup, teardown),
        cmocka_unit_test_setup_teardown(test_walk_flat_files, setup, teardown),
        cmocka_unit_test_setup_teardown(test_walk_nested_files, setup, teardown),
        cmocka_unit_test_setup_teardown(test_walk_filter_rejects_directory, setup, teardown),
        cmocka_unit_test_setup_teardown(test_walk_nonexistent_root_reports_error, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
