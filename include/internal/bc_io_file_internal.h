// SPDX-License-Identifier: MIT

#ifndef BC_IO_INTERNAL_H
#define BC_IO_INTERNAL_H

#include "bc_io_mmap.h"
#include "bc_allocators.h"

#include <stdbool.h>
#include <stddef.h>

bool bc_io_mmap_create_from_address(bc_allocators_context_t* memory_context, void* base_address, size_t mapped_size,
                                    bc_io_mmap_t** out_map);

#endif /* BC_IO_INTERNAL_H */
