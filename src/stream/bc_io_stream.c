// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_stream_internal.h"
#include "bc_io_stream_io_internal.h"

#include "bc_core.h"
#include "bc_allocators_arena.h"
#include "bc_allocators_pool.h"

#include <fcntl.h>
#include <unistd.h>

static bool bc_io_stream_allocate_with_buffer(bc_allocators_context_t* memory_context, size_t buffer_size,
                                              bc_io_stream_source_type_t source_type, bc_io_stream_t** out_stream,
                                              bc_allocators_arena_t** out_arena, void** out_buffer, size_t* out_aligned_size)
{
    size_t effective_buffer_size = buffer_size;

    if (effective_buffer_size == 0) {
        bc_io_stream_io_default_buffer_size(source_type, &effective_buffer_size);
    }

    size_t aligned_size;
    bc_io_stream_io_align_buffer_size(effective_buffer_size, &aligned_size);

    bc_io_stream_t* stream;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_io_stream_t), (void**)&stream)) {
        return false;
    }

    bc_allocators_arena_t* arena;
    if (!bc_allocators_arena_create(memory_context, aligned_size, &arena)) {
        bc_allocators_pool_free(memory_context, stream);
        return false;
    }

    void* buffer;
    if (!bc_allocators_arena_allocate(arena, aligned_size, 64, &buffer)) {
        bc_allocators_arena_destroy(arena);
        bc_allocators_pool_free(memory_context, stream);
        return false;
    }

    *out_stream = stream;
    *out_arena = arena;
    *out_buffer = buffer;
    *out_aligned_size = aligned_size;
    return true;
}

static void bc_io_stream_init_common_fields(bc_io_stream_t* stream, bc_allocators_context_t* memory_context,
                                            bc_allocators_arena_t* buffer_arena, void* buffer, size_t buffer_size,
                                            bc_io_stream_source_type_t source_type, bc_io_stream_mode_t mode, int file_descriptor,
                                            bool owns_file_descriptor)
{
    stream->memory_context = memory_context;
    stream->buffer_arena = buffer_arena;
    stream->buffer = buffer;
    stream->buffer_size = buffer_size;
    stream->buffer_used = 0;
    stream->source_type = source_type;
    stream->mode = mode;
    stream->file_descriptor = file_descriptor;
    stream->owns_file_descriptor = owns_file_descriptor;
    stream->end_of_stream = false;
    stream->memory_data = NULL;
    stream->memory_size = 0;
    stream->memory_position = 0;
    stream->total_size = 0;
    stream->total_size_known = false;
    stream->logical_position = 0;
    bc_io_stream_stats_t zero_stats = {0};
    stream->stats = zero_stats;
}

bool bc_io_stream_open_file(bc_allocators_context_t* memory_context, const char* path, bc_io_stream_mode_t mode, size_t buffer_size,
                            bc_io_stream_t** out_stream)
{
    int file_descriptor;
    if (!bc_io_stream_io_open_file(path, mode, &file_descriptor)) {
        return false;
    }

    size_t total_size = 0;
    bool total_size_known = false;

    if (mode == BC_IO_STREAM_MODE_READ) {
        if (bc_io_stream_io_get_file_size(file_descriptor, &total_size)) {
            total_size_known = true;
        }
        bc_io_stream_io_advise_sequential(file_descriptor);
    }

    bc_io_stream_t* stream;
    bc_allocators_arena_t* arena;
    void* buffer;
    size_t aligned_size;

    if (!bc_io_stream_allocate_with_buffer(memory_context, buffer_size, BC_IO_STREAM_SOURCE_FILE, &stream, &arena, &buffer,
                                           &aligned_size)) {
        close(file_descriptor);
        return false;
    }

    bc_io_stream_init_common_fields(stream, memory_context, arena, buffer, aligned_size, BC_IO_STREAM_SOURCE_FILE, mode, file_descriptor,
                                    true);
    stream->total_size = total_size;
    stream->total_size_known = total_size_known;

    *out_stream = stream;
    return true;
}

bool bc_io_stream_open_file_descriptor(bc_allocators_context_t* memory_context, int file_descriptor, bc_io_stream_source_type_t source_type,
                                       bc_io_stream_mode_t mode, size_t buffer_size, bc_io_stream_t** out_stream)
{
    size_t total_size = 0;
    bool total_size_known = false;

    if (source_type == BC_IO_STREAM_SOURCE_FILE && mode == BC_IO_STREAM_MODE_READ) {
        if (bc_io_stream_io_get_file_size(file_descriptor, &total_size)) {
            total_size_known = true;
        }
        bc_io_stream_io_advise_sequential(file_descriptor);
    }

    bc_io_stream_t* stream;
    bc_allocators_arena_t* arena;
    void* buffer;
    size_t aligned_size;

    if (!bc_io_stream_allocate_with_buffer(memory_context, buffer_size, source_type, &stream, &arena, &buffer, &aligned_size)) {
        return false;
    }

    bc_io_stream_init_common_fields(stream, memory_context, arena, buffer, aligned_size, source_type, mode, file_descriptor, false);
    stream->total_size = total_size;
    stream->total_size_known = total_size_known;

    *out_stream = stream;
    return true;
}

bool bc_io_stream_open_memory(bc_allocators_context_t* memory_context, const void* data, size_t size, bc_io_stream_t** out_stream)
{
    size_t chunk_size;
    bc_io_stream_io_default_buffer_size(BC_IO_STREAM_SOURCE_MEMORY, &chunk_size);

    bc_io_stream_t* stream;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_io_stream_t), (void**)&stream)) {
        return false;
    }

    bc_io_stream_init_common_fields(stream, memory_context, NULL, NULL, chunk_size, BC_IO_STREAM_SOURCE_MEMORY, BC_IO_STREAM_MODE_READ, -1,
                                    false);
    stream->memory_data = data;
    stream->memory_size = size;
    stream->memory_position = 0;
    stream->total_size = size;
    stream->total_size_known = true;

    *out_stream = stream;
    return true;
}

bool bc_io_stream_source_type(const bc_io_stream_t* stream, bc_io_stream_source_type_t* out_source_type)
{
    *out_source_type = stream->source_type;
    return true;
}

bool bc_io_stream_total_size(const bc_io_stream_t* stream, size_t* out_size)
{
    if (!stream->total_size_known) {
        return false;
    }
    *out_size = stream->total_size;
    return true;
}

bool bc_io_stream_current_position(const bc_io_stream_t* stream, size_t* out_position)
{
    *out_position = stream->logical_position;
    return true;
}

bool bc_io_stream_remaining_bytes(const bc_io_stream_t* stream, size_t* out_remaining)
{
    if (!stream->total_size_known) {
        return false;
    }
    *out_remaining = stream->total_size - stream->logical_position;
    return true;
}

bool bc_io_stream_is_end_of_stream(const bc_io_stream_t* stream, bool* out_is_eof)
{
    *out_is_eof = stream->end_of_stream;
    return true;
}

bool bc_io_stream_get_stats(const bc_io_stream_t* stream, bc_io_stream_stats_t* out_stats)
{
    *out_stats = stream->stats;
    return true;
}

bool bc_io_stream_advise(bc_io_stream_t* stream, bc_io_stream_access_hint_t hint)
{
    if (stream->source_type == BC_IO_STREAM_SOURCE_MEMORY) {
        return true;
    }

    int advice;
    if (hint == BC_IO_STREAM_ACCESS_SEQUENTIAL) {
        advice = POSIX_FADV_SEQUENTIAL;
    } else if (hint == BC_IO_STREAM_ACCESS_RANDOM) {
        advice = POSIX_FADV_RANDOM;
    } else {
        advice = POSIX_FADV_DONTNEED;
    }

    return posix_fadvise(stream->file_descriptor, 0, 0, advice) == 0;
}

void bc_io_stream_close(bc_io_stream_t* stream)
{
    bc_io_stream_flush(stream);

    if (stream->buffer_arena != NULL) {
        bc_allocators_arena_destroy(stream->buffer_arena);
    }

    if (stream->owns_file_descriptor) {
        close(stream->file_descriptor);
    }

    bc_allocators_context_t* memory_context = stream->memory_context;
    bc_allocators_pool_free(memory_context, stream);
}
