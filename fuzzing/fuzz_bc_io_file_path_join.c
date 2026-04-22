// SPDX-License-Identifier: MIT

#include "bc_io_file_path.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_PATH_JOIN_BUFFER_CAPACITY 1024

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 3) {
        return 0;
    }

    const uint8_t base_length = data[0];
    const uint8_t name_length = data[1];
    const uint8_t capacity_nibble = (uint8_t)(data[2] & 0x7);

    size_t position = 3;
    if (position + base_length + name_length > size) {
        return 0;
    }

    char base[256];
    char name[256];
    const size_t base_actual = base_length < sizeof(base) - 1 ? base_length : sizeof(base) - 1;
    const size_t name_actual = name_length < sizeof(name) - 1 ? name_length : sizeof(name) - 1;

    memcpy(base, data + position, base_actual);
    base[base_actual] = '\0';
    position += base_length;
    memcpy(name, data + position, name_actual);
    name[name_actual] = '\0';

    char output_buffer[FUZZ_PATH_JOIN_BUFFER_CAPACITY];
    static const size_t capacity_table[8] = {0, 1, 2, 16, 64, 256, 512, FUZZ_PATH_JOIN_BUFFER_CAPACITY};
    const size_t capacity = capacity_table[capacity_nibble];
    size_t out_length = 0;

    (void)bc_io_file_path_join(output_buffer, capacity, base, base_actual, name, name_actual, &out_length);

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

    uint8_t buffer[2048];
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
