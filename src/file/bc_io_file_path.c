// SPDX-License-Identifier: MIT

#include "bc_io_file.h"

#include "bc_core.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

bool bc_io_file_open_for_read(const char* path, int additional_flags, int* out_file_descriptor)
{
    int flags_with_noatime = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NOATIME | additional_flags;
    int file_descriptor = open(path, flags_with_noatime);
    if (file_descriptor < 0 && errno == EPERM) { /* GCOVR_EXCL_BR_LINE -- O_NOATIME rejected when caller lacks CAP_FOWNER */
        /* GCOVR_EXCL_START -- O_NOATIME fallback */
        int flags_without_noatime = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | additional_flags;
        file_descriptor = open(path, flags_without_noatime);
        /* GCOVR_EXCL_STOP */
    }
    if (file_descriptor < 0) {
        return false;
    }
    *out_file_descriptor = file_descriptor;
    return true;
}

bool bc_io_file_path_join(char* buffer, size_t buffer_capacity, const char* base, size_t base_length, const char* name, size_t name_length,
                          size_t* out_length)
{
    if (base_length == 0) {
        if (name_length + 1 > buffer_capacity) {
            return false;
        }
        if (name_length > 0) {
            bc_core_copy(buffer, name, name_length);
        }
        buffer[name_length] = '\0';
        *out_length = name_length;
        return true;
    }

    size_t total_length = base_length + 1 + name_length;
    if (total_length + 1 > buffer_capacity) {
        return false;
    }
    bc_core_copy(buffer, base, base_length);
    buffer[base_length] = '/';
    if (name_length > 0) {
        bc_core_copy(buffer + base_length + 1, name, name_length);
    }
    buffer[total_length] = '\0';
    *out_length = total_length;
    return true;
}

bool bc_io_file_dtype_to_entry_type(unsigned char d_type, bc_io_file_entry_type_t* out_type)
{
    switch (d_type) {
    case DT_REG:
        *out_type = BC_IO_ENTRY_TYPE_FILE;
        return true;
    case DT_DIR:
        *out_type = BC_IO_ENTRY_TYPE_DIRECTORY;
        return true;
    case DT_LNK:
        *out_type = BC_IO_ENTRY_TYPE_SYMLINK;
        return true;
    default:
        *out_type = BC_IO_ENTRY_TYPE_OTHER;
        return true;
    }
}

bool bc_io_file_stat_if_unknown(int directory_file_descriptor, const char* name, bc_io_file_entry_type_t* out_type, dev_t* out_device,
                                ino_t* out_inode, size_t* out_size, time_t* out_modification_time)
{
    struct stat stat_buffer;
    if (fstatat(directory_file_descriptor, name, &stat_buffer, AT_SYMLINK_NOFOLLOW) != 0) {
        return false;
    }

    if (S_ISREG(stat_buffer.st_mode)) {
        *out_type = BC_IO_ENTRY_TYPE_FILE;
    } else if (S_ISDIR(stat_buffer.st_mode)) {
        *out_type = BC_IO_ENTRY_TYPE_DIRECTORY;
    } else if (S_ISLNK(stat_buffer.st_mode)) {
        *out_type = BC_IO_ENTRY_TYPE_SYMLINK;
    } else {
        *out_type = BC_IO_ENTRY_TYPE_OTHER;
    }

    *out_device = stat_buffer.st_dev;
    *out_inode = stat_buffer.st_ino;
    *out_size = (size_t)stat_buffer.st_size;
    *out_modification_time = stat_buffer.st_mtime;
    return true;
}

bool bc_io_file_advise(int file_descriptor, size_t offset, size_t length, bc_io_mmap_madvise_hint_t hint)
{
    int posix_advice;
    switch (hint) {
    case BC_IO_MADVISE_NORMAL:
        posix_advice = POSIX_FADV_NORMAL;
        break;
    case BC_IO_MADVISE_SEQUENTIAL:
        posix_advice = POSIX_FADV_SEQUENTIAL;
        break;
    case BC_IO_MADVISE_RANDOM:
        posix_advice = POSIX_FADV_RANDOM;
        break;
    case BC_IO_MADVISE_WILLNEED:
        posix_advice = POSIX_FADV_WILLNEED;
        break;
    case BC_IO_MADVISE_DONTNEED:
        posix_advice = POSIX_FADV_DONTNEED;
        break;
    default:
        return false;
    }
    int result = posix_fadvise(file_descriptor, (off_t)offset, (off_t)length, posix_advice);
    if (result != 0) {
        errno = result;
        return false;
    }
    return true;
}
