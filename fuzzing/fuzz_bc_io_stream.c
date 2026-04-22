// SPDX-License-Identifier: MIT

#include "bc_io_stream.h"

#include "bc_allocators.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    bc_allocators_context_t* ctx = NULL;
    if (!bc_allocators_context_create(NULL, &ctx)) {
        return 0;
    }

    if (size == 0) {
        bc_allocators_context_destroy(ctx);
        return 0;
    }

    bc_io_stream_t* memory_stream = NULL;
    if (bc_io_stream_open_memory(ctx, data, size, &memory_stream)) {
        bc_io_stream_chunk_t chunk;
        while (bc_io_stream_read_chunk(memory_stream, &chunk)) {
            if (chunk.size == 0) {
                break;
            }
        }
        size_t pos = 0;
        bc_io_stream_current_position(memory_stream, &pos);
        size_t total = 0;
        bc_io_stream_total_size(memory_stream, &total);
        bool at_eof = false;
        bc_io_stream_is_end_of_stream(memory_stream, &at_eof);
        bc_io_stream_close(memory_stream);
    }

    bc_allocators_context_destroy(ctx);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    unsigned long iterations = strtoul(argv[1], NULL, 10);
    unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[4096];
    for (unsigned long i = 0; i < iterations; i++) {
        size_t len = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < len; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, len);
    }
    return 0;
}
#endif
