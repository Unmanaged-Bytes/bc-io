// SPDX-License-Identifier: MIT

#include "bc_io_file_stat.h"

#include "bc_core.h"

#include <sys/stat.h>
#include <sys/types.h>

#define BC_IO_FILE_STAT_PERMISSION_BITS_MASK ((uint32_t)07777)

static bool bc_io_file_stat_fill_from_struct_stat(const struct stat* source, bc_io_file_stat_t* out_stat)
{
    if (!bc_core_zero(out_stat, sizeof(*out_stat))) {
        return false;
    }

    const mode_t file_mode = source->st_mode;
    out_stat->is_regular = S_ISREG(file_mode) != 0;
    out_stat->is_directory = S_ISDIR(file_mode) != 0;
    out_stat->is_symlink = S_ISLNK(file_mode) != 0;
    out_stat->is_character_device = S_ISCHR(file_mode) != 0;
    out_stat->is_block_device = S_ISBLK(file_mode) != 0;
    out_stat->is_fifo = S_ISFIFO(file_mode) != 0;
    out_stat->is_socket = S_ISSOCK(file_mode) != 0;

    out_stat->size_bytes = (uint64_t)source->st_size;
    out_stat->mode = (uint32_t)file_mode & BC_IO_FILE_STAT_PERMISSION_BITS_MASK;
    out_stat->modification_time_seconds = (int64_t)source->st_mtim.tv_sec;
    out_stat->modification_time_nanoseconds = (int32_t)source->st_mtim.tv_nsec;
    out_stat->hardlink_count = (uint64_t)source->st_nlink;
    out_stat->owner_user_id = (uint32_t)source->st_uid;
    out_stat->group_id = (uint32_t)source->st_gid;
    out_stat->device_id = (uint64_t)source->st_dev;
    out_stat->inode_number = (uint64_t)source->st_ino;
    return true;
}

bool bc_io_file_stat(const char* path, bc_io_file_stat_t* out_stat)
{
    struct stat stat_buffer;
    if (stat(path, &stat_buffer) != 0) {
        return false;
    }
    return bc_io_file_stat_fill_from_struct_stat(&stat_buffer, out_stat);
}

bool bc_io_file_stat_lstat(const char* path, bc_io_file_stat_t* out_stat)
{
    struct stat stat_buffer;
    if (lstat(path, &stat_buffer) != 0) {
        return false;
    }
    return bc_io_file_stat_fill_from_struct_stat(&stat_buffer, out_stat);
}
