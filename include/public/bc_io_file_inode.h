// SPDX-License-Identifier: MIT

#ifndef BC_IO_INODE_SET_H
#define BC_IO_INODE_SET_H

#include "bc_allocators.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct bc_io_file_inode_set bc_io_file_inode_set_t;

bool bc_io_file_inode_set_create(bc_allocators_context_t* memory_context, size_t initial_capacity, bc_io_file_inode_set_t** out_set);

void bc_io_file_inode_set_destroy(bc_io_file_inode_set_t* set);

bool bc_io_file_inode_set_insert(bc_io_file_inode_set_t* set, dev_t device, ino_t inode, bool* out_was_already_present);

bool bc_io_file_inode_set_contains(const bc_io_file_inode_set_t* set, dev_t device, ino_t inode, bool* out_is_present);

bool bc_io_file_inode_set_clear(bc_io_file_inode_set_t* set);

bool bc_io_file_inode_set_get_size(const bc_io_file_inode_set_t* set, size_t* out_size);

#endif /* BC_IO_INODE_SET_H */
