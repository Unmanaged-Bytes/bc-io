// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

#define TEST_BUFFER_SIZE 256

struct test_fixture {
    bc_allocators_context_t* memory_context;
};

static int group_setup(void** state)
{
    struct test_fixture* fixture = test_calloc(1, sizeof(struct test_fixture));

    if (!bc_allocators_context_create(NULL, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }

    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct test_fixture* fixture = *state;
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static char* create_temp_file_with_data(const void* data, size_t size)
{
    char* path = test_calloc(1, 64);
    bc_core_copy(path, "/tmp/bc_io_stream_meta_XXXXXX", sizeof("/tmp/bc_io_stream_meta_XXXXXX"));
    int file_descriptor = mkstemp(path);
    if (file_descriptor < 0) {
        test_free(path);
        return NULL;
    }
    if (size > 0) {
        ssize_t written = write(file_descriptor, data, size);
        (void)written;
    }
    close(file_descriptor);
    return path;
}

static void test_source_type_file(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xAA);

    char* path = create_temp_file_with_data(data, 64);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_FILE);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_source_type_memory(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xBB);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 64, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_MEMORY);

    bc_io_stream_close(stream);
}

static void test_source_type_pipe(void** state)
{
    struct test_fixture* fixture = *state;
    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xCC);
    ssize_t written = write(pipe_fds[1], data, 32);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_PIPE);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_source_type_socket(void** state)
{
    struct test_fixture* fixture = *state;
    int socket_fds[2];
    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds), 0);

    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xDD);
    ssize_t written = write(socket_fds[1], data, 32);
    (void)written;
    shutdown(socket_fds[1], SHUT_WR);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, socket_fds[0], BC_IO_STREAM_SOURCE_SOCKET,
                                                  BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_source_type_t source_type;
    assert_true(bc_io_stream_source_type(stream, &source_type));
    assert_int_equal(source_type, BC_IO_STREAM_SOURCE_SOCKET);

    bc_io_stream_close(stream);
    close(socket_fds[0]);
    close(socket_fds[1]);
}

static void test_total_size_file(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[1000];
    bc_core_fill(data, 1000, (unsigned char)0xEE);

    char* path = create_temp_file_with_data(data, 1000);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t total_size;
    assert_true(bc_io_stream_total_size(stream, &total_size));
    assert_int_equal((int)total_size, 1000);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_total_size_memory(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[500];
    bc_core_fill(data, 500, (unsigned char)0xFF);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, data, 500, &stream));

    size_t total_size;
    assert_true(bc_io_stream_total_size(stream, &total_size));
    assert_int_equal((int)total_size, 500);

    bc_io_stream_close(stream);
}

static void test_total_size_pipe_fails(void** state)
{
    struct test_fixture* fixture = *state;
    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    size_t total_size;
    assert_false(bc_io_stream_total_size(stream, &total_size));

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_total_size_socket_fails(void** state)
{
    struct test_fixture* fixture = *state;
    int socket_fds[2];
    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds), 0);
    shutdown(socket_fds[1], SHUT_WR);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, socket_fds[0], BC_IO_STREAM_SOURCE_SOCKET,
                                                  BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t total_size;
    assert_false(bc_io_stream_total_size(stream, &total_size));

    bc_io_stream_close(stream);
    close(socket_fds[0]);
    close(socket_fds[1]);
}

static void test_current_position_initial(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0x11);

    char* path = create_temp_file_with_data(data, 64);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t position;
    assert_true(bc_io_stream_current_position(stream, &position));
    assert_int_equal((int)position, 0);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_current_position_after_read(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0x22);

    char* path = create_temp_file_with_data(data, 100);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));

    size_t position;
    assert_true(bc_io_stream_current_position(stream, &position));
    assert_int_equal((int)position, 100);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_remaining_bytes_file(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[200];
    bc_core_fill(data, 200, (unsigned char)0x33);

    char* path = create_temp_file_with_data(data, 200);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    size_t remaining;
    assert_true(bc_io_stream_remaining_bytes(stream, &remaining));
    assert_int_equal((int)remaining, 200);

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));

    assert_true(bc_io_stream_remaining_bytes(stream, &remaining));
    assert_int_equal((int)remaining, 0);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_remaining_bytes_pipe_fails(void** state)
{
    struct test_fixture* fixture = *state;
    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  TEST_BUFFER_SIZE, &stream));

    size_t remaining;
    assert_false(bc_io_stream_remaining_bytes(stream, &remaining));

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_is_eof_initial_false(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0x44);

    char* path = create_temp_file_with_data(data, 64);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bool is_eof;
    assert_true(bc_io_stream_is_end_of_stream(stream, &is_eof));
    assert_false(is_eof);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_is_eof_after_complete_read(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0x55);

    char* path = create_temp_file_with_data(data, 64);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, TEST_BUFFER_SIZE, &stream));

    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
    }

    bool is_eof;
    assert_true(bc_io_stream_is_end_of_stream(stream, &is_eof));
    assert_true(is_eof);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_source_type_file),         cmocka_unit_test(test_source_type_memory),
        cmocka_unit_test(test_source_type_pipe),         cmocka_unit_test(test_source_type_socket),
        cmocka_unit_test(test_total_size_file),          cmocka_unit_test(test_total_size_memory),
        cmocka_unit_test(test_total_size_pipe_fails),    cmocka_unit_test(test_total_size_socket_fails),
        cmocka_unit_test(test_current_position_initial), cmocka_unit_test(test_current_position_after_read),
        cmocka_unit_test(test_remaining_bytes_file),     cmocka_unit_test(test_remaining_bytes_pipe_fails),
        cmocka_unit_test(test_is_eof_initial_false),     cmocka_unit_test(test_is_eof_after_complete_read),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
