// SPDX-License-Identifier: MIT

#ifndef BC_IO_HELPERS_H
#define BC_IO_HELPERS_H

#include "bc_io_mmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

typedef enum {
    BC_IO_ENTRY_TYPE_FILE = 0,
    BC_IO_ENTRY_TYPE_DIRECTORY = 1,
    BC_IO_ENTRY_TYPE_SYMLINK = 2,
    BC_IO_ENTRY_TYPE_OTHER = 3,
} bc_io_file_entry_type_t;

bool bc_io_file_open_for_read(const char* path, int additional_flags, int* out_file_descriptor);

bool bc_io_file_path_join(char* buffer, size_t buffer_capacity, const char* base, size_t base_length, const char* name, size_t name_length,
                          size_t* out_length);

bool bc_io_file_dtype_to_entry_type(unsigned char d_type, bc_io_file_entry_type_t* out_type);

bool bc_io_file_stat_if_unknown(int directory_file_descriptor, const char* name, bc_io_file_entry_type_t* out_type, dev_t* out_device,
                                ino_t* out_inode, size_t* out_size, time_t* out_modification_time);

bool bc_io_file_advise(int file_descriptor, size_t offset, size_t length, bc_io_mmap_madvise_hint_t hint);

#endif /* BC_IO_HELPERS_H */
