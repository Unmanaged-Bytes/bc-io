// SPDX-License-Identifier: MIT

#ifndef BC_IO_PATH_H
#define BC_IO_PATH_H

#include <stdbool.h>
#include <stddef.h>

bool bc_io_path_current_directory(char* output_buffer, size_t output_capacity, size_t* out_length);

#endif /* BC_IO_PATH_H */
