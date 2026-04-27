// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_io_file_inode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(NULL, &memory_context)) {
        return 0;
    }
    bc_io_file_inode_set_t* inode_set = NULL;
    if (!bc_io_file_inode_set_create(memory_context, 8, &inode_set)) {
        bc_allocators_context_destroy(memory_context);
        return 0;
    }

    size_t position = 0;
    while (position + 17 <= size) {
        const uint8_t operation = data[position++] & 0x3;
        dev_t device = 0;
        ino_t inode = 0;
        memcpy(&device, &data[position], sizeof(device));
        position += sizeof(device);
        memcpy(&inode, &data[position], sizeof(inode));
        position += sizeof(inode);

        switch (operation) {
        case 0: {
            bool already_present = false;
            (void)bc_io_file_inode_set_insert(inode_set, device, inode, &already_present);
            break;
        }
        case 1: {
            bool is_present = false;
            (void)bc_io_file_inode_set_contains(inode_set, device, inode, &is_present);
            break;
        }
        case 2: {
            size_t current_size = 0;
            (void)bc_io_file_inode_set_get_size(inode_set, &current_size);
            break;
        }
        default:
            (void)bc_io_file_inode_set_clear(inode_set);
            break;
        }
    }

    bc_io_file_inode_set_destroy(inode_set);
    bc_allocators_context_destroy(memory_context);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    const unsigned long iterations = strtoul(argv[1], NULL, 10);
    const unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[4096];
    for (unsigned long i = 0; i < iterations; i++) {
        const size_t length = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < length; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, length);
    }
    return 0;
}
#endif
