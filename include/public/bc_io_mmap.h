// SPDX-License-Identifier: MIT

#ifndef BC_IO_MAP_H
#define BC_IO_MAP_H

#include "bc_allocators.h"
#include "bc_io_stream.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    BC_IO_MADVISE_NORMAL = 0,
    BC_IO_MADVISE_SEQUENTIAL = 1,
    BC_IO_MADVISE_RANDOM = 2,
    BC_IO_MADVISE_WILLNEED = 3,
    BC_IO_MADVISE_DONTNEED = 4
} bc_io_mmap_madvise_hint_t;

typedef struct bc_io_mmap_options {
    bool read_only;
    bool populate;
    bool hugepages_hint;
    bc_io_mmap_madvise_hint_t madvise_hint;
    size_t offset;
    size_t length;
} bc_io_mmap_options_t;

typedef struct bc_io_file_map bc_io_mmap_t;

bool bc_io_mmap_file(bc_allocators_context_t* memory_context, const char* path, const bc_io_mmap_options_t* options,
                     bc_io_mmap_t** out_map);

bool bc_io_mmap_get_stream(bc_io_mmap_t* map, bc_io_stream_t** out_stream);

bool bc_io_mmap_get_data(const bc_io_mmap_t* map, const void** out_data, size_t* out_size);

bool bc_io_mmap_advise(bc_io_mmap_t* map, size_t offset, size_t length, bc_io_mmap_madvise_hint_t hint);

bool bc_io_mmap_unmap(bc_io_mmap_t* map);

void bc_io_mmap_destroy(bc_io_mmap_t* map);

#endif /* BC_IO_MAP_H */
