// SPDX-License-Identifier: MIT

#include "bc_io_file_inode.h"

#include "bc_core.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BC_IO_INODE_SET_DEFAULT_CAPACITY ((size_t)8192)

typedef struct bc_io_file_inode_set_entry {
    dev_t device;
    ino_t inode;
} bc_io_file_inode_set_entry_t;

struct bc_io_file_inode_set {
    bc_allocators_context_t* memory_context;
    bc_io_file_inode_set_entry_t* entries;
    uint8_t* occupied;
    size_t capacity;
    size_t size;
};

static size_t bc_io_file_inode_set_round_up_to_power_of_two(size_t value)
{
    if (value < 2) {
        return 2;
    }
    size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static size_t bc_io_file_inode_set_hash(dev_t device, ino_t inode, size_t capacity)
{
    uint64_t x = (uint64_t)device * UINT64_C(0x9E3779B97F4A7C15);
    x ^= (uint64_t)inode + UINT64_C(0x9E3779B97F4A7C15) + (x << 6) + (x >> 2);
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return (size_t)(x & (uint64_t)(capacity - 1));
}

static bool bc_io_file_inode_set_allocate_buckets(bc_allocators_context_t* memory_context, size_t capacity,
                                                  bc_io_file_inode_set_entry_t** out_entries, uint8_t** out_occupied)
{
    size_t entries_size = 0;
    if (!bc_core_safe_multiply(capacity, sizeof(bc_io_file_inode_set_entry_t), &entries_size)) {
        errno = ENOMEM;
        return false;
    }
    size_t total_size = 0;
    if (!bc_core_safe_add(entries_size, capacity, &total_size)) {
        errno = ENOMEM;
        return false;
    }
    void* mapped = NULL;
    if (!bc_allocators_pool_allocate(memory_context, total_size, &mapped)) {
        return false;
    }
    bc_core_zero(mapped, total_size);
    *out_entries = (bc_io_file_inode_set_entry_t*)mapped;
    *out_occupied = (uint8_t*)((char*)mapped + entries_size);
    return true;
}

bool bc_io_file_inode_set_create(bc_allocators_context_t* memory_context, size_t initial_capacity, bc_io_file_inode_set_t** out_set)
{
    size_t capacity = initial_capacity;
    if (capacity == 0) {
        capacity = BC_IO_INODE_SET_DEFAULT_CAPACITY;
    }
    capacity = bc_io_file_inode_set_round_up_to_power_of_two(capacity);

    void* set_memory = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_io_file_inode_set_t), &set_memory)) {
        return false;
    }
    bc_io_file_inode_set_t* set = set_memory;
    set->memory_context = memory_context;
    set->capacity = capacity;
    set->size = 0;
    set->entries = NULL;
    set->occupied = NULL;

    if (!bc_io_file_inode_set_allocate_buckets(memory_context, capacity, &set->entries, &set->occupied)) {
        bc_allocators_pool_free(memory_context, set);
        return false;
    }

    *out_set = set;
    return true;
}

void bc_io_file_inode_set_destroy(bc_io_file_inode_set_t* set)
{
    bc_allocators_pool_free(set->memory_context, set->entries);
    bc_allocators_pool_free(set->memory_context, set);
}

static bool bc_io_file_inode_set_grow(bc_io_file_inode_set_t* set)
{
    size_t new_capacity = set->capacity * 2;

    bc_io_file_inode_set_entry_t* new_entries = NULL;
    uint8_t* new_occupied = NULL;
    if (!bc_io_file_inode_set_allocate_buckets(set->memory_context, new_capacity, &new_entries, &new_occupied)) {
        return false;
    }

    for (size_t index = 0; index < set->capacity; ++index) {
        if (!set->occupied[index]) {
            continue;
        }
        dev_t device = set->entries[index].device;
        ino_t inode = set->entries[index].inode;
        size_t mask = new_capacity - 1;
        size_t slot = bc_io_file_inode_set_hash(device, inode, new_capacity);
        while (new_occupied[slot]) {
            slot = (slot + 1) & mask;
        }
        new_entries[slot].device = device;
        new_entries[slot].inode = inode;
        new_occupied[slot] = 1;
    }

    bc_allocators_pool_free(set->memory_context, set->entries);
    set->entries = new_entries;
    set->occupied = new_occupied;
    set->capacity = new_capacity;
    return true;
}

bool bc_io_file_inode_set_insert(bc_io_file_inode_set_t* set, dev_t device, ino_t inode, bool* out_was_already_present)
{
    if ((set->size + 1) * 4 > set->capacity * 3) {
        if (!bc_io_file_inode_set_grow(set)) {
            return false;
        }
    }

    size_t mask = set->capacity - 1;
    size_t slot = bc_io_file_inode_set_hash(device, inode, set->capacity);
    while (set->occupied[slot]) {
        if (set->entries[slot].device == device && set->entries[slot].inode == inode) {
            *out_was_already_present = true;
            return true;
        }
        slot = (slot + 1) & mask;
    }
    set->entries[slot].device = device;
    set->entries[slot].inode = inode;
    set->occupied[slot] = 1;
    set->size += 1;
    *out_was_already_present = false;

    return true;
}

bool bc_io_file_inode_set_contains(const bc_io_file_inode_set_t* set, dev_t device, ino_t inode, bool* out_is_present)
{
    size_t mask = set->capacity - 1;
    size_t slot = bc_io_file_inode_set_hash(device, inode, set->capacity);
    while (set->occupied[slot]) {
        if (set->entries[slot].device == device && set->entries[slot].inode == inode) {
            *out_is_present = true;
            return true;
        }
        slot = (slot + 1) & mask;
    }
    *out_is_present = false;
    return true;
}

bool bc_io_file_inode_set_clear(bc_io_file_inode_set_t* set)
{
    bc_core_zero(set->occupied, set->capacity);
    set->size = 0;
    return true;
}

bool bc_io_file_inode_set_get_size(const bc_io_file_inode_set_t* set, size_t* out_size)
{
    *out_size = set->size;
    return true;
}
