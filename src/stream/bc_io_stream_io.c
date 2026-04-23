// SPDX-License-Identifier: MIT

#include "bc_io_stream_io_internal.h"

#include "bc_core.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BC_IO_STREAM_DEFAULT_FILE_BUFFER_SIZE 131072
#define BC_IO_STREAM_DEFAULT_SOCKET_BUFFER_SIZE 32768
#define BC_IO_STREAM_DEFAULT_PIPE_BUFFER_SIZE 65536
#define BC_IO_STREAM_DEFAULT_MEMORY_CHUNK_SIZE 131072

#define BC_IO_STREAM_BUFFER_ALIGNMENT 64

bool bc_io_stream_io_read_full(int file_descriptor, void* buffer, size_t requested_size, size_t* out_bytes_read, bool* out_eof)
{
    *out_bytes_read = 0;
    *out_eof = false;

    unsigned char* destination = (unsigned char*)buffer;
    size_t total_read = 0;

    while (total_read < requested_size) {
        ssize_t result = read(file_descriptor, destination + total_read, requested_size - total_read);

        if (result > 0) {
            total_read += (size_t)result;
        } else if (result == 0) {
            *out_eof = true;
            break;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                break;
            }
            *out_bytes_read = total_read;
            return false;
        }
    }

    *out_bytes_read = total_read;
    return true;
}

bool bc_io_stream_io_write_full(int file_descriptor, const void* data, size_t size, size_t* out_bytes_written)
{
    *out_bytes_written = 0;

    const unsigned char* source = (const unsigned char*)data;
    size_t total_written = 0;

    while (total_written < size) {
        ssize_t result = write(file_descriptor, source + total_written, size - total_written);

        if (result >= 0) {
            total_written += (size_t)result;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                break;
            }
            *out_bytes_written = total_written;
            return false;
        }
    }

    *out_bytes_written = total_written;
    return true;
}

bool bc_io_stream_io_open_file(const char* path, bc_io_stream_mode_t mode, int* out_file_descriptor)
{
    int flags;

    if (mode == BC_IO_STREAM_MODE_READ) {
        flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
    } else {
        flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW;
    }

    int file_descriptor = open(path, flags, 0644);
    if (file_descriptor < 0) {
        return false;
    }

    *out_file_descriptor = file_descriptor;
    return true;
}

bool bc_io_stream_io_get_file_size(int file_descriptor, size_t* out_size)
{
    struct stat file_stat;

    if (fstat(file_descriptor, &file_stat) != 0) {
        return false;
    }

    if (!S_ISREG(file_stat.st_mode)) {
        return false;
    }

    *out_size = (size_t)file_stat.st_size;
    return true;
}

bool bc_io_stream_io_advise_sequential(int file_descriptor)
{
    int result = posix_fadvise(file_descriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
    return result == 0;
}

bool bc_io_stream_io_default_buffer_size(bc_io_stream_source_type_t source_type, size_t* out_buffer_size)
{
    switch (source_type) {
    case BC_IO_STREAM_SOURCE_FILE:
        *out_buffer_size = BC_IO_STREAM_DEFAULT_FILE_BUFFER_SIZE;
        return true;
    case BC_IO_STREAM_SOURCE_SOCKET:
        *out_buffer_size = BC_IO_STREAM_DEFAULT_SOCKET_BUFFER_SIZE;
        return true;
    case BC_IO_STREAM_SOURCE_PIPE:
        *out_buffer_size = BC_IO_STREAM_DEFAULT_PIPE_BUFFER_SIZE;
        return true;
    default:
        *out_buffer_size = BC_IO_STREAM_DEFAULT_MEMORY_CHUNK_SIZE;
        return true;
    }
}

bool bc_io_stream_io_align_buffer_size(size_t requested_size, size_t* out_aligned_size)
{
    return bc_core_align_up(requested_size, BC_IO_STREAM_BUFFER_ALIGNMENT, out_aligned_size);
}
