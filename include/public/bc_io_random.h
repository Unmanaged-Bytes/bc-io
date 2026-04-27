// SPDX-License-Identifier: MIT

#ifndef BC_IO_RANDOM_H
#define BC_IO_RANDOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool bc_io_random_bytes(void* output, size_t length);
bool bc_io_random_unsigned_integer_64(uint64_t* out_value);

#endif /* BC_IO_RANDOM_H */
