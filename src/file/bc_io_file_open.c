// SPDX-License-Identifier: MIT

#include "bc_io_file_open.h"

#include "bc_io_file_internal.h"

#include "bc_io_file_path.h"
#include "bc_io_mmap.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_io_stream.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BC_IO_OPEN_DEFAULT_BUFFER_SIZE ((size_t)65536)
#define BC_IO_OPEN_DEFAULT_THRESHOLD ((size_t)(256 * 1024))

typedef enum { BC_IO_READ_HANDLE_KIND_BUFFERED = 0, BC_IO_READ_HANDLE_KIND_MEMORY_MAPPED = 1 } bc_io_file_read_handle_kind_t;

struct bc_io_file_read_handle {
    bc_allocators_context_t* memory_context;
    bc_io_file_read_handle_kind_t kind;
    bc_io_stream_t* stream;
    bc_io_mmap_t* map;
    size_t file_size;
};

static int bc_io_file_open_options_flags(const bc_io_file_open_options_t* options)
{
    int additional = 0;
    if (options->nonblock) {
        additional |= O_NONBLOCK;
    }
    return additional;
}

static bool bc_io_file_open_resolve_descriptor(const char* path, const bc_io_file_open_options_t* options, int* out_file_descriptor)
{
    int additional_flags = bc_io_file_open_options_flags(options);
    if (options->use_noatime) {
        return bc_io_file_open_for_read(path, additional_flags, out_file_descriptor);
    }
    int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | additional_flags;
    int file_descriptor = open(path, flags);
    if (file_descriptor < 0) {
        return false;
    }
    *out_file_descriptor = file_descriptor;
    return true;
}

bool bc_io_file_open_read(bc_allocators_context_t* memory_context, const char* path, const bc_io_file_open_options_t* options,
                          bc_io_stream_t** out_stream)
{
    int file_descriptor = -1;
    if (!bc_io_file_open_resolve_descriptor(path, options, &file_descriptor)) {
        return false;
    }

    size_t buffer_size = BC_IO_OPEN_DEFAULT_BUFFER_SIZE;
    if (options->buffer_size != 0) {
        buffer_size = options->buffer_size;
    }

    bc_io_stream_t* stream = NULL;
    if (!bc_io_stream_open_file_descriptor(memory_context, file_descriptor, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ, buffer_size,
                                           &stream)) { /* GCOVR_EXCL_BR_LINE -- allocator failure path */
        /* GCOVR_EXCL_START -- allocator failure path */
        close(file_descriptor);
        return false;
        /* GCOVR_EXCL_STOP */
    }
    *out_stream = stream;
    return true;
}

bool bc_io_file_open_auto(bc_allocators_context_t* memory_context, const char* path, size_t mmap_threshold,
                          const bc_io_file_open_options_t* options, bc_io_file_read_handle_t** out_handle)
{
    size_t effective_threshold = mmap_threshold;
    if (effective_threshold == 0) {
        effective_threshold = BC_IO_OPEN_DEFAULT_THRESHOLD;
    }

    int file_descriptor = -1;
    if (!bc_io_file_open_resolve_descriptor(path, options, &file_descriptor)) {
        return false;
    }

    struct stat stat_buffer;
    if (fstat(file_descriptor, &stat_buffer) != 0) { /* GCOVR_EXCL_BR_LINE -- OS-level fstat failure */
        /* GCOVR_EXCL_START -- OS-level fstat failure */
        close(file_descriptor);
        return false;
        /* GCOVR_EXCL_STOP */
    }

    size_t file_size = (size_t)stat_buffer.st_size;

    void* handle_memory = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_io_file_read_handle_t),
                                     &handle_memory)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        /* GCOVR_EXCL_START -- allocator failure */
        close(file_descriptor);
        return false;
        /* GCOVR_EXCL_STOP */
    }
    bc_io_file_read_handle_t* handle = handle_memory;
    handle->memory_context = memory_context;
    handle->file_size = file_size;
    handle->stream = NULL;
    handle->map = NULL;

    if (file_size < effective_threshold) {
        size_t buffer_size = BC_IO_OPEN_DEFAULT_BUFFER_SIZE;
        if (options->buffer_size != 0) {
            buffer_size = options->buffer_size;
        }
        bc_io_stream_t* stream = NULL;
        if (!bc_io_stream_open_file_descriptor(memory_context, file_descriptor, BC_IO_STREAM_SOURCE_FILE, BC_IO_STREAM_MODE_READ,
                                               buffer_size, &stream)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
            /* GCOVR_EXCL_START -- allocator failure */
            close(file_descriptor);
            bc_allocators_pool_free(memory_context, handle);
            return false;
            /* GCOVR_EXCL_STOP */
        }
        handle->kind = BC_IO_READ_HANDLE_KIND_BUFFERED;
        handle->stream = stream;
        *out_handle = handle;
        return true;
    }

    void* base_address = NULL;
    if (file_size > 0) {
        base_address = mmap(NULL, file_size, PROT_READ, MAP_SHARED, file_descriptor, (off_t)0);
        if (base_address == MAP_FAILED) { /* GCOVR_EXCL_BR_LINE -- OS-level mmap failure */
            /* GCOVR_EXCL_START -- OS-level mmap failure */
            close(file_descriptor);
            bc_allocators_pool_free(memory_context, handle);
            return false;
            /* GCOVR_EXCL_STOP */
        }
    }
    close(file_descriptor);

    if (!bc_io_mmap_create_from_address(memory_context, base_address, file_size,
                                        &handle->map)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        /* GCOVR_EXCL_START -- allocator failure */
        if (base_address != NULL) {
            munmap(base_address, file_size);
        }
        bc_allocators_pool_free(memory_context, handle);
        return false;
        /* GCOVR_EXCL_STOP */
    }

    bc_io_stream_t* stream = NULL;
    if (!bc_io_mmap_get_stream(handle->map, &stream)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        /* GCOVR_EXCL_START -- allocator failure */
        bc_io_mmap_destroy(handle->map);
        bc_allocators_pool_free(memory_context, handle);
        return false;
        /* GCOVR_EXCL_STOP */
    }
    handle->kind = BC_IO_READ_HANDLE_KIND_MEMORY_MAPPED;
    handle->stream = stream;

    *out_handle = handle;
    return true;
}

bool bc_io_file_read_handle_get_stream(bc_io_file_read_handle_t* handle, bc_io_stream_t** out_stream)
{
    *out_stream = handle->stream;
    return true;
}

bool bc_io_file_read_handle_is_memory_mapped(const bc_io_file_read_handle_t* handle, bool* out_is_mapped)
{
    *out_is_mapped = (handle->kind == BC_IO_READ_HANDLE_KIND_MEMORY_MAPPED);
    return true;
}

bool bc_io_file_read_handle_get_size(const bc_io_file_read_handle_t* handle, size_t* out_size)
{
    *out_size = handle->file_size;
    return true;
}

void bc_io_file_read_handle_destroy(bc_io_file_read_handle_t* handle)
{
    if (handle->kind == BC_IO_READ_HANDLE_KIND_MEMORY_MAPPED) {
        if (handle->map != NULL) {
            bc_io_mmap_destroy(handle->map);
        }
    } else {
        if (handle->stream != NULL) {
            bc_io_stream_close(handle->stream);
        }
    }
    bc_allocators_pool_free(handle->memory_context, handle);
}
