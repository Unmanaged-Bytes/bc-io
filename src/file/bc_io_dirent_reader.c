// SPDX-License-Identifier: MIT

#include "bc_io_dirent_reader.h"

#include "bc_allocators_pool.h"
#include "bc_core.h"

#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define BC_IO_DIRENT_READER_BUFFER_SIZE ((size_t)8192)

struct bc_io_dirent_reader {
    int dir_fd;
    int last_errno;
    ssize_t buffer_used;
    size_t cursor;
    char buffer[BC_IO_DIRENT_READER_BUFFER_SIZE];
};

struct bc_io_linux_dirent64 {
    /* cppcheck-suppress unusedStructMember; required by kernel ABI */
    uint64_t d_ino;
    /* cppcheck-suppress unusedStructMember; required by kernel ABI */
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

bool bc_io_dirent_reader_create(bc_allocators_context_t* memory_context, int dir_fd, bc_io_dirent_reader_t** out_reader)
{
    *out_reader = NULL;
    bc_io_dirent_reader_t* reader = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(*reader), (void**)&reader)) {
        return false;
    }
    reader->dir_fd = dir_fd;
    reader->last_errno = 0;
    reader->buffer_used = 0;
    reader->cursor = 0;
    *out_reader = reader;
    return true;
}

void bc_io_dirent_reader_destroy(bc_allocators_context_t* memory_context, bc_io_dirent_reader_t* reader)
{
    if (reader == NULL) {
        return;
    }
    bc_allocators_pool_free(memory_context, reader);
}

void bc_io_dirent_reader_reset(bc_io_dirent_reader_t* reader, int dir_fd)
{
    reader->dir_fd = dir_fd;
    reader->last_errno = 0;
    reader->buffer_used = 0;
    reader->cursor = 0;
}

bool bc_io_dirent_reader_next(bc_io_dirent_reader_t* reader, bc_io_dirent_entry_t* out_entry, bool* out_has_entry)
{
    for (;;) {
        if ((ssize_t)reader->cursor >= reader->buffer_used) {
            ssize_t bytes_read = syscall(SYS_getdents64, reader->dir_fd, reader->buffer, sizeof(reader->buffer));
            if (bytes_read < 0) {
                reader->last_errno = errno;
                return false;
            }
            if (bytes_read == 0) {
                *out_has_entry = false;
                return true;
            }
            reader->buffer_used = bytes_read;
            reader->cursor = 0;
        }

        const struct bc_io_linux_dirent64* record = (const struct bc_io_linux_dirent64*)(reader->buffer + reader->cursor);
        reader->cursor += record->d_reclen;

        const char* name = record->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        size_t name_length = 0;
        if (!bc_core_length(name, '\0', &name_length)) {
            reader->last_errno = EINVAL;
            return false;
        }

        out_entry->name = name;
        out_entry->name_length = name_length;
        out_entry->d_type = record->d_type;
        *out_has_entry = true;
        return true;
    }
}

bool bc_io_dirent_reader_last_errno(const bc_io_dirent_reader_t* reader, int* out_errno)
{
    *out_errno = reader->last_errno;
    return true;
}
