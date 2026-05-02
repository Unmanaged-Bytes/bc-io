// SPDX-License-Identifier: MIT

#include "bc_io_path.h"

#include "bc_core.h"

#include <unistd.h>

bool bc_io_path_current_directory(char* output_buffer, size_t output_capacity, size_t* out_length)
{
    if (getcwd(output_buffer, output_capacity) == NULL) {
        return false;
    }
    size_t length = 0;
    if (!bc_core_length(output_buffer, 0, &length)) {
        return false;
    }
    *out_length = length;
    return true;
}
