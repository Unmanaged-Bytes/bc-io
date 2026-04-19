// SPDX-License-Identifier: MIT

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

struct bc_io_file_map {
    bc_allocators_context_t* memory_context;
    void* base_address;
    size_t mapped_size;
    bc_io_stream_t* stream;
    bool is_mapped;
};

static int bc_io_mmap_hint_to_madvise(bc_io_mmap_madvise_hint_t hint)
{
    switch (hint) {
    case BC_IO_MADVISE_NORMAL:
        return MADV_NORMAL;
    case BC_IO_MADVISE_SEQUENTIAL:
        return MADV_SEQUENTIAL;
    case BC_IO_MADVISE_RANDOM:
        return MADV_RANDOM;
    case BC_IO_MADVISE_WILLNEED:
        return MADV_WILLNEED;
    case BC_IO_MADVISE_DONTNEED:
        return MADV_DONTNEED;
    default:                /* GCOVR_EXCL_LINE -- unreachable, all enum values handled */
        return MADV_NORMAL; /* GCOVR_EXCL_LINE -- unreachable */
    }
}

bool bc_io_mmap_create_from_address(bc_allocators_context_t* memory_context, void* base_address, size_t mapped_size, bc_io_mmap_t** out_map)
{
    void* map_memory = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_io_mmap_t), &map_memory)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        return false;                                                                      /* GCOVR_EXCL_LINE -- allocator failure */
    }
    bc_io_mmap_t* map = map_memory;
    map->memory_context = memory_context;
    map->base_address = base_address;
    map->mapped_size = mapped_size;
    map->stream = NULL;
    map->is_mapped = true;
    *out_map = map;
    return true;
}

bool bc_io_mmap_file(bc_allocators_context_t* memory_context, const char* path, const bc_io_mmap_options_t* options, bc_io_mmap_t** out_map)
{
    size_t page_size = 0;
    if (!bc_allocators_context_page_size(memory_context, &page_size)) { /* GCOVR_EXCL_BR_LINE -- context getter never fails */
        return false;                                                   /* GCOVR_EXCL_LINE -- context getter never fails */
    }

    if (options->offset != 0 && (options->offset % page_size) != 0) {
        errno = EINVAL;
        return false;
    }

    int file_descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (file_descriptor < 0) { /* GCOVR_EXCL_BR_LINE -- OS-level open failure */
        return false;          /* GCOVR_EXCL_LINE -- OS-level open failure */
    }

    struct stat stat_buffer;
    if (fstat(file_descriptor, &stat_buffer) != 0) { /* GCOVR_EXCL_BR_LINE -- OS-level fstat failure */
        /* GCOVR_EXCL_START -- OS-level fstat failure */
        close(file_descriptor);
        return false;
        /* GCOVR_EXCL_STOP */
    }

    size_t file_size = (size_t)stat_buffer.st_size;
    size_t mapped_size = options->length;
    size_t mmap_offset = options->offset;
    if (mapped_size == 0) {
        if (mmap_offset > file_size) {
            close(file_descriptor);
            errno = EINVAL;
            return false;
        }
        mapped_size = file_size - mmap_offset;
    }

    void* base_address = NULL;
    if (mapped_size > 0) {
        int mmap_flags = MAP_SHARED;
        if (options->populate) {
            mmap_flags |= MAP_POPULATE;
        }
        if (options->hugepages_hint) {
            mmap_flags |= MAP_HUGETLB;
        }
        base_address = mmap(NULL, mapped_size, PROT_READ, mmap_flags, file_descriptor, (off_t)mmap_offset);
        if (base_address == MAP_FAILED) { /* GCOVR_EXCL_BR_LINE -- mmap failure with hugepages triggers fallback */
            /* GCOVR_EXCL_START -- mmap failure path */
            if (options->hugepages_hint) {
                mmap_flags &= ~MAP_HUGETLB;
                base_address = mmap(NULL, mapped_size, PROT_READ, mmap_flags, file_descriptor, (off_t)mmap_offset);
            }
            if (base_address == MAP_FAILED) {
                close(file_descriptor);
                return false;
            }
            /* GCOVR_EXCL_STOP */
        }
        if (options->madvise_hint != BC_IO_MADVISE_NORMAL) {
            int madvise_hint = bc_io_mmap_hint_to_madvise(options->madvise_hint);
            (void)madvise(base_address, mapped_size, madvise_hint);
        }
    }

    close(file_descriptor);

    if (!bc_io_mmap_create_from_address(memory_context, base_address, mapped_size, out_map)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        /* GCOVR_EXCL_START -- allocator failure */
        if (base_address != NULL) {
            munmap(base_address, mapped_size);
        }
        return false;
        /* GCOVR_EXCL_STOP */
    }
    return true;
}

bool bc_io_mmap_get_stream(bc_io_mmap_t* map, bc_io_stream_t** out_stream)
{
    if (map->stream != NULL) {
        *out_stream = map->stream;
        return true;
    }
    bc_io_stream_t* stream = NULL;
    if (!bc_io_stream_open_memory(map->memory_context, map->base_address, map->mapped_size,
                                  &stream)) { /* GCOVR_EXCL_BR_LINE -- allocator failure */
        return false;                         /* GCOVR_EXCL_LINE -- allocator failure */
    }
    map->stream = stream;
    *out_stream = stream;
    return true;
}

bool bc_io_mmap_get_data(const bc_io_mmap_t* map, const void** out_data, size_t* out_size)
{
    *out_data = map->base_address;
    *out_size = map->mapped_size;
    return true;
}

bool bc_io_mmap_advise(bc_io_mmap_t* map, size_t offset, size_t length, bc_io_mmap_madvise_hint_t hint)
{
    if (!map->is_mapped || map->base_address == NULL || length == 0) {
        return true;
    }
    int madvise_hint = bc_io_mmap_hint_to_madvise(hint);
    void* target = (char*)map->base_address + offset;
    if (madvise(target, length, madvise_hint) != 0) { /* GCOVR_EXCL_BR_LINE -- OS-level madvise failure */
        return false;                                 /* GCOVR_EXCL_LINE -- OS-level madvise failure */
    }
    return true;
}

bool bc_io_mmap_unmap(bc_io_mmap_t* map)
{
    if (!map->is_mapped) {
        return true;
    }
    if (map->base_address != NULL && map->mapped_size > 0) {
        munmap(map->base_address, map->mapped_size);
    }
    map->base_address = NULL;
    map->mapped_size = 0;
    map->is_mapped = false;
    return true;
}

void bc_io_mmap_destroy(bc_io_mmap_t* map)
{
    if (map->stream != NULL) {
        bc_io_stream_close(map->stream);
        map->stream = NULL;
    }
    if (map->is_mapped && map->base_address != NULL && map->mapped_size > 0) {
        munmap(map->base_address, map->mapped_size);
    }
    bc_allocators_pool_free(map->memory_context, map);
}
