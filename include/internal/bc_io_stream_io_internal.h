// SPDX-License-Identifier: MIT

#ifndef BC_IO_STREAM_IO_H
#define BC_IO_STREAM_IO_H

#include "bc_io_stream.h"

#include <stdbool.h>
#include <stddef.h>

bool bc_io_stream_io_read_full(int file_descriptor, void* buffer, size_t requested_size, size_t* out_bytes_read, bool* out_eof);
bool bc_io_stream_io_write_full(int file_descriptor, const void* data, size_t size, size_t* out_bytes_written);
bool bc_io_stream_io_open_file(const char* path, bc_io_stream_mode_t mode, int* out_file_descriptor);
bool bc_io_stream_io_get_file_size(int file_descriptor, size_t* out_size);
bool bc_io_stream_io_advise_sequential(int file_descriptor);
bool bc_io_stream_io_default_buffer_size(bc_io_stream_source_type_t source_type, size_t* out_buffer_size);
bool bc_io_stream_io_align_buffer_size(size_t requested_size, size_t* out_aligned_size);

#endif /* BC_IO_STREAM_IO_H */
