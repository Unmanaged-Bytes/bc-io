// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"
#include "bc_core.h"
#include "bc_io_stream_internal.h"
#include "bc_io_stream_io_internal.h"

bool bc_io_stream_flush(bc_io_stream_t* stream)
{
    if (stream->buffer_used == 0) {
        return true;
    }

    size_t original_used = stream->buffer_used;
    size_t bytes_written = 0;
    bool write_ok = bc_io_stream_io_write_full(stream->file_descriptor, stream->buffer, original_used, &bytes_written);

    size_t remaining = original_used - bytes_written;

    bc_core_move(stream->buffer, (unsigned char*)stream->buffer + bytes_written, remaining);
    stream->buffer_used = remaining;

    if (bytes_written < original_used) {
        stream->stats.short_write_count++;
        return false;
    }

    return write_ok;
}

bool bc_io_stream_write_chunk(bc_io_stream_t* stream, const void* data, size_t size, size_t* out_bytes_written)
{
    if (stream->mode != BC_IO_STREAM_MODE_WRITE) {
        *out_bytes_written = 0;
        return false;
    }

    if (stream->source_type == BC_IO_STREAM_SOURCE_MEMORY) {
        *out_bytes_written = 0;
        return false;
    }

    if (size == 0) {
        *out_bytes_written = 0;
        return true;
    }

    size_t remaining_buffer = stream->buffer_size - stream->buffer_used;

    if (size <= remaining_buffer) {
        bc_core_copy((unsigned char*)stream->buffer + stream->buffer_used, data, size);
        stream->buffer_used += size;
        *out_bytes_written = size;
        stream->stats.bytes_written += size;
        stream->stats.write_count++;
        stream->logical_position += size;
        return true;
    }

    if (stream->buffer_used > 0) {
        if (!bc_io_stream_flush(stream)) {
            *out_bytes_written = 0;
            return false;
        }
    }

    if (size >= stream->buffer_size) {
        bool ok = bc_io_stream_io_write_full(stream->file_descriptor, data, size, out_bytes_written);
        stream->stats.bytes_written += *out_bytes_written;
        stream->stats.write_count++;
        stream->logical_position += *out_bytes_written;
        if (*out_bytes_written < size) {
            stream->stats.short_write_count++;
        }
        return ok;
    }

    bc_core_copy(stream->buffer, data, size);
    stream->buffer_used = size;
    *out_bytes_written = size;
    stream->stats.bytes_written += size;
    stream->stats.write_count++;
    stream->logical_position += size;
    return true;
}
