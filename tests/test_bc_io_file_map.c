// SPDX-License-Identifier: MIT

#include "bc_core.h"
#include "bc_io_file.h"
#include "bc_allocators.h"
#include "bc_io_stream.h"

#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>

#define KNOWN_FILE_SIZE ((size_t)(16 * 1024))

typedef struct fixture {
    bc_allocators_context_t* memory_context;
    char root_directory[256];
    char known_file_path[512];
    char empty_file_path[512];
    char missing_file_path[512];
    unsigned char known_content[KNOWN_FILE_SIZE];
} fixture_t;

static void write_buffer_to_file(const char* path, const void* data, size_t size)
{
    int file_descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert_true(file_descriptor >= 0);
    if (size > 0) {
        const unsigned char* bytes = data;
        size_t written = 0;
        while (written < size) {
            ssize_t wrote_now = write(file_descriptor, bytes + written, size - written);
            assert_true(wrote_now > 0);
            written += (size_t)wrote_now;
        }
    }
    close(file_descriptor);
}

static int setup_map_fixture(void** state)
{
    fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);

    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));

    snprintf(fixture->root_directory, sizeof(fixture->root_directory), "/tmp/bc_io_mmap_%d_%ld", (int)getpid(), (long)time(NULL));
    assert_int_equal(mkdir(fixture->root_directory, 0755), 0);

    snprintf(fixture->known_file_path, sizeof(fixture->known_file_path), "%s/known.bin", fixture->root_directory);
    for (size_t index = 0; index < KNOWN_FILE_SIZE; ++index) {
        fixture->known_content[index] = (unsigned char)('A' + (index % 26));
    }
    write_buffer_to_file(fixture->known_file_path, fixture->known_content, KNOWN_FILE_SIZE);

    snprintf(fixture->empty_file_path, sizeof(fixture->empty_file_path), "%s/empty.bin", fixture->root_directory);
    write_buffer_to_file(fixture->empty_file_path, NULL, 0);

    snprintf(fixture->missing_file_path, sizeof(fixture->missing_file_path), "%s/missing.bin", fixture->root_directory);

    *state = fixture;
    return 0;
}

static int teardown_map_fixture(void** state)
{
    fixture_t* fixture = *state;
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf %s", fixture->root_directory);
    (void)system(command);
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

/* ========== map_file ========== */

static void test_map_file_regular(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
    assert_non_null(map);

    const void* data = NULL;
    size_t size = 0;
    assert_true(bc_io_mmap_get_data(map, &data, &size));
    assert_non_null(data);
    assert_int_equal(size, KNOWN_FILE_SIZE);

    bool equal = false;
    bc_core_equal(data, fixture->known_content, KNOWN_FILE_SIZE, &equal);
    assert_true(equal);

    bc_io_mmap_destroy(map);
}

static void test_map_file_get_stream_reads_full_content(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    bc_io_stream_t* stream = NULL;
    assert_true(bc_io_mmap_get_stream(map, &stream));
    assert_non_null(stream);

    size_t bytes_read = 0;
    while (bytes_read < KNOWN_FILE_SIZE) {
        bc_io_stream_chunk_t chunk = {0};
        if (!bc_io_stream_read_chunk(stream, &chunk)) {
            break;
        }
        if (chunk.size == 0) {
            break;
        }
        bool equal = false;
        bc_core_equal(chunk.data, fixture->known_content + bytes_read, chunk.size, &equal);
        assert_true(equal);
        bytes_read += chunk.size;
    }
    assert_int_equal(bytes_read, KNOWN_FILE_SIZE);

    bc_io_mmap_destroy(map);
}

static void test_map_file_empty(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->empty_file_path, &options, &map));

    const void* data = NULL;
    size_t size = 1;
    assert_true(bc_io_mmap_get_data(map, &data, &size));
    assert_int_equal(size, 0);

    assert_true(bc_io_mmap_unmap(map));
    bc_io_mmap_destroy(map);
}

static void test_map_file_missing(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_false(bc_io_mmap_file(fixture->memory_context, fixture->missing_file_path, &options, &map));
}

static void test_map_file_offset_not_page_aligned(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.offset = 1;
    options.length = 4096;
    bc_io_mmap_t* map = NULL;
    assert_false(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
}

static void test_map_file_partial(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.offset = 4096;
    options.length = 8192;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    const void* data = NULL;
    size_t size = 0;
    assert_true(bc_io_mmap_get_data(map, &data, &size));
    assert_int_equal(size, 8192);

    bool equal = false;
    bc_core_equal(data, fixture->known_content + 4096, 8192, &equal);
    assert_true(equal);

    bc_io_mmap_destroy(map);
}

static void test_map_advise_all_hints(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    assert_true(bc_io_mmap_advise(map, 0, KNOWN_FILE_SIZE, BC_IO_MADVISE_NORMAL));
    assert_true(bc_io_mmap_advise(map, 0, KNOWN_FILE_SIZE, BC_IO_MADVISE_SEQUENTIAL));
    assert_true(bc_io_mmap_advise(map, 0, KNOWN_FILE_SIZE, BC_IO_MADVISE_RANDOM));
    assert_true(bc_io_mmap_advise(map, 0, KNOWN_FILE_SIZE, BC_IO_MADVISE_WILLNEED));
    assert_true(bc_io_mmap_advise(map, 0, KNOWN_FILE_SIZE, BC_IO_MADVISE_DONTNEED));

    bc_io_mmap_destroy(map);
}

static void test_map_double_unmap(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    assert_true(bc_io_mmap_unmap(map));
    assert_true(bc_io_mmap_unmap(map));

    bc_io_mmap_destroy(map);
}

static void test_map_destroy_without_explicit_unmap(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
    bc_io_mmap_destroy(map);
}

static void test_map_file_offset_exceeds_file_size(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.offset = 1024 * 1024 * 1024; /* Much larger than KNOWN_FILE_SIZE */
    options.length = 0;                  /* Derive from file_size - offset */
    bc_io_mmap_t* map = NULL;
    assert_false(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
}

static void test_map_file_with_populate_flag(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.populate = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
    bc_io_mmap_destroy(map);
}

static void test_map_file_with_hugepages_hint_falls_back(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.hugepages_hint = true;
    bc_io_mmap_t* map = NULL;
    /* MAP_HUGETLB usually fails on standard tmpfs/ext4 files — the code
       must fall back to a plain mmap retry and succeed. */
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
    bc_io_mmap_destroy(map);
}

static void test_map_file_with_madvise_sequential_hint(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    options.madvise_hint = BC_IO_MADVISE_SEQUENTIAL;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));
    bc_io_mmap_destroy(map);
}

static void test_map_get_stream_cached_on_second_call(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    bc_io_stream_t* first_stream = NULL;
    assert_true(bc_io_mmap_get_stream(map, &first_stream));
    bc_io_stream_t* second_stream = NULL;
    assert_true(bc_io_mmap_get_stream(map, &second_stream));
    assert_ptr_equal(first_stream, second_stream);

    bc_io_mmap_destroy(map);
}

static void test_map_advise_on_unmapped_is_noop(void** state)
{
    fixture_t* fixture = *state;
    bc_io_mmap_options_t options = {0};
    options.read_only = true;
    bc_io_mmap_t* map = NULL;
    assert_true(bc_io_mmap_file(fixture->memory_context, fixture->known_file_path, &options, &map));

    assert_true(bc_io_mmap_unmap(map));
    /* map_advise on an already-unmapped map must be a no-op returning true. */
    assert_true(bc_io_mmap_advise(map, 0, 4096, BC_IO_MADVISE_SEQUENTIAL));

    bc_io_mmap_destroy(map);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_map_file_regular, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_get_stream_reads_full_content, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_empty, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_missing, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_offset_not_page_aligned, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_partial, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_advise_all_hints, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_double_unmap, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_destroy_without_explicit_unmap, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_offset_exceeds_file_size, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_with_populate_flag, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_with_hugepages_hint_falls_back, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_file_with_madvise_sequential_hint, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_get_stream_cached_on_second_call, setup_map_fixture, teardown_map_fixture),
        cmocka_unit_test_setup_teardown(test_map_advise_on_unmapped_is_noop, setup_map_fixture, teardown_map_fixture),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
