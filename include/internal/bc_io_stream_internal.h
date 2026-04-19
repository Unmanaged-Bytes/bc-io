// SPDX-License-Identifier: MIT

#ifndef BC_IO_STREAM_INTERNAL_H
#define BC_IO_STREAM_INTERNAL_H

#include "bc_allocators_arena.h"
#include "bc_io_stream.h"

struct bc_io_stream {
    bc_allocators_context_t* memory_context;
    bc_allocators_arena_t* buffer_arena;
    void* buffer;
    size_t buffer_size;
    size_t buffer_used;

    bc_io_stream_source_type_t source_type;
    bc_io_stream_mode_t mode;

    int file_descriptor;
    bool owns_file_descriptor;
    bool end_of_stream;

    const void* memory_data;
    size_t memory_size;
    size_t memory_position;

    size_t total_size;
    bool total_size_known;

    size_t logical_position;

    bc_io_stream_stats_t stats;
};

#endif /* BC_IO_STREAM_INTERNAL_H */
