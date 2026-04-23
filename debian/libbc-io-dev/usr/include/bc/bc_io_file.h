// SPDX-License-Identifier: MIT

#ifndef BC_IO_H
#define BC_IO_H

#include "bc_io_file_path.h"
#include "bc_io_file_inode.h"
#include "bc_io_mmap.h"
#include "bc_io_file_open.h"

#include <stddef.h>

#define BC_IO_DEFAULT_GETDENTS_BUFFER_SIZE ((size_t)32768)
#define BC_IO_MAX_PATH_LENGTH ((size_t)4096)
#define BC_IO_MMAP_DEFAULT_THRESHOLD ((size_t)(256 * 1024))

#endif /* BC_IO_H */
