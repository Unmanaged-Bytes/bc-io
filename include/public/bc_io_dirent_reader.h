// SPDX-License-Identifier: MIT

#ifndef BC_IO_DIRENT_READER_H
#define BC_IO_DIRENT_READER_H

#include "bc_allocators.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct bc_io_dirent_reader bc_io_dirent_reader_t;

typedef struct bc_io_dirent_entry {
    const char* name;
    size_t name_length;
    unsigned char d_type;
} bc_io_dirent_entry_t;

bool bc_io_dirent_reader_create(bc_allocators_context_t* memory_context, int dir_fd, bc_io_dirent_reader_t** out_reader);

void bc_io_dirent_reader_destroy(bc_allocators_context_t* memory_context, bc_io_dirent_reader_t* reader);

void bc_io_dirent_reader_reset(bc_io_dirent_reader_t* reader, int dir_fd);

bool bc_io_dirent_reader_next(bc_io_dirent_reader_t* reader, bc_io_dirent_entry_t* out_entry, bool* out_has_entry);

bool bc_io_dirent_reader_last_errno(const bc_io_dirent_reader_t* reader, int* out_errno);

#endif /* BC_IO_DIRENT_READER_H */
