// SPDX-License-Identifier: MIT

#ifndef BC_IO_FILE_H
#define BC_IO_FILE_H

#include "bc_io_mmap.h"
#include "bc_allocators.h"
#include "bc_io_stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define BC_IO_DEFAULT_GETDENTS_BUFFER_SIZE ((size_t)32768)
#define BC_IO_MAX_PATH_LENGTH ((size_t)4096)
#define BC_IO_MMAP_DEFAULT_THRESHOLD ((size_t)(256 * 1024))

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

typedef struct bc_io_file_open_options {
    bool use_noatime;
    bool nonblock;
    bc_io_mmap_madvise_hint_t fadvise_hint;
    size_t buffer_size;
} bc_io_file_open_options_t;

bool bc_io_file_open_read(bc_allocators_context_t* memory_context, const char* path, const bc_io_file_open_options_t* options,
                          bc_io_stream_t** out_stream);

typedef struct bc_io_file_read_handle bc_io_file_read_handle_t;

bool bc_io_file_open_auto(bc_allocators_context_t* memory_context, const char* path, size_t mmap_threshold,
                          const bc_io_file_open_options_t* options, bc_io_file_read_handle_t** out_handle);

bool bc_io_file_read_handle_get_stream(bc_io_file_read_handle_t* handle, bc_io_stream_t** out_stream);

bool bc_io_file_read_handle_is_memory_mapped(const bc_io_file_read_handle_t* handle, bool* out_is_mapped);

bool bc_io_file_read_handle_get_size(const bc_io_file_read_handle_t* handle, size_t* out_size);

void bc_io_file_read_handle_destroy(bc_io_file_read_handle_t* handle);

#endif /* BC_IO_FILE_H */
