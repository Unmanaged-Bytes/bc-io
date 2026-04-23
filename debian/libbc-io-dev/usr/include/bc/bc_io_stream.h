// SPDX-License-Identifier: MIT

#ifndef BC_IO_STREAM_H
#define BC_IO_STREAM_H

#include "bc_allocators.h"

#include <stdbool.h>
#include <stddef.h>

/* ===== Types ===== */

typedef enum {
    BC_IO_STREAM_SOURCE_FILE,
    BC_IO_STREAM_SOURCE_SOCKET,
    BC_IO_STREAM_SOURCE_MEMORY,
    BC_IO_STREAM_SOURCE_PIPE,
} bc_io_stream_source_type_t;

typedef enum {
    BC_IO_STREAM_MODE_READ,
    BC_IO_STREAM_MODE_WRITE,
} bc_io_stream_mode_t;

typedef enum {
    BC_IO_STREAM_ACCESS_SEQUENTIAL,
    BC_IO_STREAM_ACCESS_RANDOM,
    BC_IO_STREAM_ACCESS_DROP,
} bc_io_stream_access_hint_t;

typedef struct bc_io_stream bc_io_stream_t;

/* ===== Chunk ===== */

typedef struct bc_io_stream_chunk {
    const void* data;
    size_t size;
    bool owned;
} bc_io_stream_chunk_t;

/* ===== Statistics ===== */

typedef struct bc_io_stream_stats {
    size_t bytes_read;
    size_t bytes_written;
    size_t read_count;
    size_t write_count;
    size_t short_read_count;
    size_t short_write_count;
    size_t retry_count;
} bc_io_stream_stats_t;

/* ===== Creation ===== */

bool bc_io_stream_open_file(bc_allocators_context_t* memory_context, const char* path, bc_io_stream_mode_t mode, size_t buffer_size,
                            bc_io_stream_t** out_stream);

bool bc_io_stream_open_file_descriptor(bc_allocators_context_t* memory_context, int file_descriptor, bc_io_stream_source_type_t source_type,
                                       bc_io_stream_mode_t mode, size_t buffer_size, bc_io_stream_t** out_stream);

bool bc_io_stream_open_memory(bc_allocators_context_t* memory_context, const void* data, size_t size, bc_io_stream_t** out_stream);

/* ===== Read ===== */

bool bc_io_stream_read_chunk(bc_io_stream_t* stream, bc_io_stream_chunk_t* out_chunk);

/* ===== Write ===== */

bool bc_io_stream_write_chunk(bc_io_stream_t* stream, const void* data, size_t size, size_t* out_bytes_written);

bool bc_io_stream_flush(bc_io_stream_t* stream);

/* ===== Metadata ===== */

bool bc_io_stream_source_type(const bc_io_stream_t* stream, bc_io_stream_source_type_t* out_source_type);
bool bc_io_stream_total_size(const bc_io_stream_t* stream, size_t* out_size);
bool bc_io_stream_current_position(const bc_io_stream_t* stream, size_t* out_position);
bool bc_io_stream_remaining_bytes(const bc_io_stream_t* stream, size_t* out_remaining);
bool bc_io_stream_is_end_of_stream(const bc_io_stream_t* stream, bool* out_is_eof);

/* ===== Statistics ===== */

bool bc_io_stream_get_stats(const bc_io_stream_t* stream, bc_io_stream_stats_t* out_stats);

/* ===== Advise ===== */

bool bc_io_stream_advise(bc_io_stream_t* stream, bc_io_stream_access_hint_t hint);

/* ===== Destruction ===== */

void bc_io_stream_close(bc_io_stream_t* stream);

#endif /* BC_IO_STREAM_H */
