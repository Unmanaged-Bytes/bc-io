// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_io_stream_internal.h"
#include "bc_io_stream_io_internal.h"

static bool bc_io_stream_read_chunk_file(bc_io_stream_t* stream, bc_io_stream_chunk_t* out_chunk)
{
    size_t bytes_read;
    bool eof;

    if (!bc_io_stream_io_read_full(stream->file_descriptor, stream->buffer, stream->buffer_size, &bytes_read, &eof)) {
        return false;
    }

    if (bytes_read == 0 && eof) {
        stream->end_of_stream = true;
        return false;
    }

    out_chunk->data = stream->buffer;
    out_chunk->size = bytes_read;
    out_chunk->owned = true;

    stream->stats.bytes_read += bytes_read;
    stream->stats.read_count++;

    if (bytes_read < stream->buffer_size) {
        stream->stats.short_read_count++;
    }

    if (eof) {
        stream->end_of_stream = true;
    }

    stream->logical_position += bytes_read;

    return true;
}

static bool bc_io_stream_read_chunk_memory(bc_io_stream_t* stream, bc_io_stream_chunk_t* out_chunk)
{
    if (stream->memory_position >= stream->memory_size) {
        stream->end_of_stream = true;
        return false;
    }

    size_t remaining = stream->memory_size - stream->memory_position;
    size_t chunk_size = remaining < stream->buffer_size ? remaining : stream->buffer_size;

    out_chunk->data = (const unsigned char*)stream->memory_data + stream->memory_position;
    out_chunk->size = chunk_size;
    out_chunk->owned = false;

    stream->memory_position += chunk_size;
    stream->logical_position += chunk_size;

    stream->stats.bytes_read += chunk_size;
    stream->stats.read_count++;

    return true;
}

bool bc_io_stream_read_chunk(bc_io_stream_t* stream, bc_io_stream_chunk_t* out_chunk)
{
    if (stream->end_of_stream) {
        return false;
    }

    if (stream->mode != BC_IO_STREAM_MODE_READ) {
        return false;
    }

    if (stream->source_type == BC_IO_STREAM_SOURCE_MEMORY) {
        return bc_io_stream_read_chunk_memory(stream, out_chunk);
    }

    return bc_io_stream_read_chunk_file(stream, out_chunk);
}
