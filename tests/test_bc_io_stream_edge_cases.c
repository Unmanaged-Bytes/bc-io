// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include "bc_core.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

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
    bc_core_copy(path, "/tmp/bc_io_stream_edge_XXXXXX", sizeof("/tmp/bc_io_stream_edge_XXXXXX"));
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

static void test_buffer_size_zero_uses_default(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xAA);

    char* path = create_temp_file_with_data(data, 100);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 0, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);
    assert_memory_equal(chunk.data, data, 100);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_buffer_size_not_aligned(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[200];
    bc_core_fill(data, 200, (unsigned char)0xBB);

    char* path = create_temp_file_with_data(data, 200);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 100, &stream));

    size_t total_read = 0;
    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
        total_read += chunk.size;
    }
    assert_int_equal((int)total_read, 200);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_buffer_size_exact_alignment(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[256];
    bc_core_fill(data, 256, (unsigned char)0xCC);

    char* path = create_temp_file_with_data(data, 256);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 128, &stream));

    size_t total_read = 0;
    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
        total_read += chunk.size;
    }
    assert_int_equal((int)total_read, 256);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_read_after_eof_returns_false(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xDD);

    char* path = create_temp_file_with_data(data, 32);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 256, &stream));

    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
    }

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_multiple_reads_after_eof(void** state)
{
    struct test_fixture* fixture = *state;
    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xEE);

    char* path = create_temp_file_with_data(data, 32);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 256, &stream));

    bc_io_stream_chunk_t chunk;
    while (bc_io_stream_read_chunk(stream, &chunk)) {
    }

    assert_false(bc_io_stream_read_chunk(stream, &chunk));
    assert_false(bc_io_stream_read_chunk(stream, &chunk));
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_write_zero_bytes(void** state)
{
    struct test_fixture* fixture = *state;
    const char* path = "/tmp/bc_io_stream_test_edge_write_zero.bin";

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_WRITE, 0, &stream));

    const unsigned char data[1] = {0};
    size_t bytes_written = 999;
    assert_true(bc_io_stream_write_chunk(stream, data, 0, &bytes_written));
    assert_int_equal((int)bytes_written, 0);

    bc_io_stream_close(stream);
    unlink(path);
}

static void test_open_memory_size_zero_data_null(void** state)
{
    struct test_fixture* fixture = *state;

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_memory(fixture->memory_context, NULL, 0, &stream));

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
}

static void test_open_file_nonexistent(void** state)
{
    struct test_fixture* fixture = *state;

    bc_io_stream_t* stream;
    assert_false(bc_io_stream_open_file(fixture->memory_context, "/tmp/bc_io_stream_nonexistent_path_that_does_not_exist.bin",
                                        BC_IO_STREAM_MODE_READ, 0, &stream));
}

static void test_read_chunk_on_closed_file_descriptor(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xAA);
    ssize_t written = write(pipe_fds[1], data, 64);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  256, &stream));

    close(pipe_fds[0]);

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bc_io_stream_close(stream);
}

static void test_write_chunk_on_closed_file_descriptor(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE,
                                                  256, &stream));

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0xBB);
    size_t bytes_written = 999;
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));
    assert_false(bc_io_stream_flush(stream));

    bc_io_stream_close(stream);
}

static void test_read_empty_file(void** state)
{
    struct test_fixture* fixture = *state;

    char* path = create_temp_file_with_data(NULL, 0);
    assert_non_null(path);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file(fixture->memory_context, path, BC_IO_STREAM_MODE_READ, 256, &stream));

    size_t total_size;
    assert_true(bc_io_stream_total_size(stream, &total_size));
    assert_int_equal((int)total_size, 0);

    bc_io_stream_chunk_t chunk;
    assert_false(bc_io_stream_read_chunk(stream, &chunk));

    bool is_eof;
    bc_io_stream_is_end_of_stream(stream, &is_eof);
    assert_true(is_eof);

    bc_io_stream_close(stream);
    unlink(path);
    test_free(path);
}

static void test_write_chunk_on_memory_source_type_via_file_descriptor(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[1], BC_IO_STREAM_SOURCE_MEMORY, BC_IO_STREAM_MODE_WRITE,
                                                  256, &stream));

    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xCC);
    size_t bytes_written = 999;
    assert_false(bc_io_stream_write_chunk(stream, data, 32, &bytes_written));
    assert_int_equal((int)bytes_written, 0);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

static void test_open_file_descriptor_pipe_not_regular_file(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    unsigned char data[32];
    bc_core_fill(data, 32, (unsigned char)0xDD);
    ssize_t written = write(pipe_fds[1], data, 32);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ,
                                                  256, &stream));

    size_t total_size;
    assert_false(bc_io_stream_total_size(stream, &total_size));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 32);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_buffer_size_zero_pipe(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xEE);
    ssize_t written = write(pipe_fds[1], data, 100);
    (void)written;
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ, 0,
                                                  &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
}

static void test_buffer_size_zero_socket(void** state)
{
    struct test_fixture* fixture = *state;

    int socket_fds[2];
    assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds), 0);

    unsigned char data[100];
    bc_core_fill(data, 100, (unsigned char)0xFF);
    ssize_t written = write(socket_fds[1], data, 100);
    (void)written;
    shutdown(socket_fds[1], SHUT_WR);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, socket_fds[0], BC_IO_STREAM_SOURCE_SOCKET,
                                                  BC_IO_STREAM_MODE_READ, 0, &stream));

    bc_io_stream_chunk_t chunk;
    assert_true(bc_io_stream_read_chunk(stream, &chunk));
    assert_int_equal((int)chunk.size, 100);

    bc_io_stream_close(stream);
    close(socket_fds[0]);
    close(socket_fds[1]);
}

static void test_open_file_descriptor_file_write_mode(void** state)
{
    struct test_fixture* fixture = *state;
    const char* path = "/tmp/bc_io_stream_edge_fd_write.bin";

    int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    assert_true(file_descriptor >= 0);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, file_descriptor, BC_IO_STREAM_SOURCE_FILE,
                                                  BC_IO_STREAM_MODE_WRITE, 0, &stream));

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0x77);
    size_t bytes_written = 0;
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));
    assert_int_equal((int)bytes_written, 64);

    bc_io_stream_close(stream);
    close(file_descriptor);
    unlink(path);
}

static void test_write_to_broken_pipe(void** state)
{
    struct test_fixture* fixture = *state;

    struct sigaction old_action;
    struct sigaction ignore_action;
    bc_core_zero(&ignore_action, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ignore_action, &old_action);

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    close(pipe_fds[0]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE,
                                                  256, &stream));

    unsigned char data[64];
    bc_core_fill(data, 64, (unsigned char)0x88);
    size_t bytes_written = 999;
    assert_true(bc_io_stream_write_chunk(stream, data, 64, &bytes_written));
    assert_false(bc_io_stream_flush(stream));

    bc_io_stream_close(stream);
    close(pipe_fds[1]);

    sigaction(SIGPIPE, &old_action, NULL);
}

static void test_read_nonblocking_pipe_eagain(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  256, &stream));

    bc_io_stream_chunk_t chunk;
    bool result = bc_io_stream_read_chunk(stream, &chunk);
    (void)result;

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

static void test_write_nonblocking_pipe_eagain(void** state)
{
    struct test_fixture* fixture = *state;

    struct sigaction old_action;
    struct sigaction ignore_action;
    bc_core_zero(&ignore_action, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ignore_action, &old_action);

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);

    int flags = fcntl(pipe_fds[1], F_GETFL, 0);
    fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[1], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_WRITE,
                                                  256, &stream));

    unsigned char large_data[1048576];
    bc_core_fill(large_data, sizeof(large_data), (unsigned char)0x99);

    size_t bytes_written = 0;
    bc_io_stream_write_chunk(stream, large_data, sizeof(large_data), &bytes_written);

    bc_io_stream_close(stream);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    sigaction(SIGPIPE, &old_action, NULL);
}

static void test_close_stream_without_owned_file_descriptor(void** state)
{
    struct test_fixture* fixture = *state;

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    close(pipe_fds[1]);

    bc_io_stream_t* stream;
    assert_true(bc_io_stream_open_file_descriptor(fixture->memory_context, pipe_fds[0], BC_IO_STREAM_SOURCE_PIPE, BC_IO_STREAM_MODE_READ,
                                                  256, &stream));

    bc_io_stream_close(stream);

    close(pipe_fds[0]);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_buffer_size_zero_uses_default),
        cmocka_unit_test(test_buffer_size_not_aligned),
        cmocka_unit_test(test_buffer_size_exact_alignment),
        cmocka_unit_test(test_read_after_eof_returns_false),
        cmocka_unit_test(test_multiple_reads_after_eof),
        cmocka_unit_test(test_write_zero_bytes),
        cmocka_unit_test(test_open_memory_size_zero_data_null),
        cmocka_unit_test(test_open_file_nonexistent),
        cmocka_unit_test(test_read_chunk_on_closed_file_descriptor),
        cmocka_unit_test(test_write_chunk_on_closed_file_descriptor),
        cmocka_unit_test(test_read_empty_file),
        cmocka_unit_test(test_write_chunk_on_memory_source_type_via_file_descriptor),
        cmocka_unit_test(test_open_file_descriptor_pipe_not_regular_file),
        cmocka_unit_test(test_buffer_size_zero_pipe),
        cmocka_unit_test(test_buffer_size_zero_socket),
        cmocka_unit_test(test_open_file_descriptor_file_write_mode),
        cmocka_unit_test(test_write_to_broken_pipe),
        cmocka_unit_test(test_read_nonblocking_pipe_eagain),
        cmocka_unit_test(test_write_nonblocking_pipe_eagain),
        cmocka_unit_test(test_close_stream_without_owned_file_descriptor),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
