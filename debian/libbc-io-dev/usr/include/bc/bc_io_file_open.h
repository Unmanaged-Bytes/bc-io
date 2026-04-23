// SPDX-License-Identifier: MIT

#ifndef BC_IO_OPEN_H
#define BC_IO_OPEN_H

#include "bc_io_mmap.h"
#include "bc_allocators.h"
#include "bc_io_stream.h"

#include <stdbool.h>
#include <stddef.h>

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

#endif /* BC_IO_OPEN_H */
