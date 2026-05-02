// SPDX-License-Identifier: MIT

#ifndef BC_IO_FILE_STAT_H
#define BC_IO_FILE_STAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool is_regular;
    bool is_directory;
    bool is_symlink;
    bool is_character_device;
    bool is_block_device;
    bool is_fifo;
    bool is_socket;
    uint64_t size_bytes;
    uint32_t mode;
    int64_t modification_time_seconds;
    int32_t modification_time_nanoseconds;
    uint64_t hardlink_count;
    uint32_t owner_user_id;
    uint32_t group_id;
    uint64_t device_id;
    uint64_t inode_number;
} bc_io_file_stat_t;

bool bc_io_file_stat(const char* path, bc_io_file_stat_t* out_stat);
bool bc_io_file_stat_lstat(const char* path, bc_io_file_stat_t* out_stat);

#endif /* BC_IO_FILE_STAT_H */
