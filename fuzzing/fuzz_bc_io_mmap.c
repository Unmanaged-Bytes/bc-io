// SPDX-License-Identifier: MIT

#include "bc_io_mmap.h"

#include "bc_allocators.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 4) {
        return 0;
    }

    bc_allocators_context_t* ctx = NULL;
    if (!bc_allocators_context_create(NULL, &ctx)) {
        return 0;
    }

    char path[] = "/tmp/bc_io_mmap_fuzz_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        bc_allocators_context_destroy(ctx);
        return 0;
    }
    ssize_t written = write(fd, data, size);
    (void)written;
    close(fd);

    bc_io_mmap_options_t options = {
        .offset = 0,
        .length = 0,
        .read_only = true,
        .populate = (data[0] & 1) != 0,
        .hugepages_hint = (data[1] & 1) != 0,
        .madvise_hint = (bc_io_mmap_madvise_hint_t)(data[2] % 5),
    };

    bc_io_mmap_t* map = NULL;
    if (bc_io_mmap_file(ctx, path, &options, &map)) {
        const void* base = NULL;
        size_t mapped_size = 0;
        bc_io_mmap_get_data(map, &base, &mapped_size);

        bc_io_mmap_advise(map, 0, mapped_size, (bc_io_mmap_madvise_hint_t)(data[3] % 5));
        bc_io_mmap_destroy(map);
    }

    unlink(path);
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

    uint8_t buffer[1024];
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
