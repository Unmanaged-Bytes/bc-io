// SPDX-License-Identifier: MIT

#include "bc_io_walk.h"

#include "bc_io_dirent_reader.h"
#include "bc_io_file.h"

#include "bc_concurrency_signal.h"
#include "bc_core.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct bc_io_walk_sequential_state {
    const bc_io_walk_config_t* config;
    bc_io_walk_stats_t stats;
    bool visit_failed;
} bc_io_walk_sequential_state_t;

static bool bc_io_walk_sequential_should_stop(const bc_io_walk_sequential_state_t* state)
{
    if (state->config->signal_handler == NULL) {
        return false;
    }
    bool should_stop = false;
    bc_concurrency_signal_handler_should_stop(state->config->signal_handler, &should_stop);
    return should_stop;
}

static void bc_io_walk_sequential_report_error(bc_io_walk_sequential_state_t* state, const char* path, const char* stage, int errno_value)
{
    state->stats.errors_encountered += 1;
    if (state->config->on_error != NULL) {
        state->config->on_error(path, stage, errno_value, state->config->error_user_data);
    }
}

static bool bc_io_walk_sequential_entry_kind_from_type(bc_io_file_entry_type_t type, bc_io_walk_entry_kind_t* out_kind)
{
    switch (type) {
    case BC_IO_ENTRY_TYPE_FILE:
        *out_kind = BC_IO_WALK_ENTRY_FILE;
        return true;
    case BC_IO_ENTRY_TYPE_DIRECTORY:
        *out_kind = BC_IO_WALK_ENTRY_DIRECTORY;
        return true;
    case BC_IO_ENTRY_TYPE_SYMLINK:
        *out_kind = BC_IO_WALK_ENTRY_SYMLINK;
        return true;
    case BC_IO_ENTRY_TYPE_OTHER:
    default:
        *out_kind = BC_IO_WALK_ENTRY_OTHER;
        return true;
    }
}

static void bc_io_walk_sequential_recurse(bc_io_walk_sequential_state_t* state, const char* directory_path, size_t directory_path_length,
                                          size_t directory_depth)
{
    if (state->visit_failed) {
        return;
    }
    if (bc_io_walk_sequential_should_stop(state)) {
        return;
    }

    int open_flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
    if (!state->config->follow_symlinks) {
        open_flags |= O_NOFOLLOW;
    }
    int directory_file_descriptor = open(directory_path, open_flags);
    if (directory_file_descriptor < 0) {
        bc_io_walk_sequential_report_error(state, directory_path, "open", errno);
        return;
    }

    state->stats.directories_visited += 1;

    bc_io_dirent_reader_t* dirent_reader = NULL;
    if (!bc_io_dirent_reader_create(state->config->main_memory_context, directory_file_descriptor, &dirent_reader)) {
        bc_io_walk_sequential_report_error(state, directory_path, "dirent-reader-alloc", ENOMEM);
        close(directory_file_descriptor);
        return;
    }

    for (;;) {
        if (state->visit_failed) {
            break;
        }
        if (bc_io_walk_sequential_should_stop(state)) {
            break;
        }

        bc_io_dirent_entry_t current_entry;
        bool has_entry = false;
        if (!bc_io_dirent_reader_next(dirent_reader, &current_entry, &has_entry)) {
            int reader_errno = 0;
            bc_io_dirent_reader_last_errno(dirent_reader, &reader_errno);
            bc_io_walk_sequential_report_error(state, directory_path, "getdents64", reader_errno);
            break;
        }
        if (!has_entry) {
            break;
        }
        if (current_entry.name[0] == '.' && !state->config->include_hidden) {
            continue;
        }

        char child_path_buffer[BC_IO_MAX_PATH_LENGTH];
        size_t child_path_length = 0;
        if (!bc_io_file_path_join(child_path_buffer, sizeof(child_path_buffer), directory_path, directory_path_length, current_entry.name,
                                  current_entry.name_length, &child_path_length)) {
            bc_io_walk_sequential_report_error(state, directory_path, "path-too-long", ENAMETOOLONG);
            continue;
        }

        bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
        size_t resolved_file_size = 0;
        dev_t resolved_device_id = 0;
        ino_t resolved_inode_number = 0;
        long resolved_modification_time = 0;
        unsigned int resolved_permission_mask = 0;
        bool stat_populated = false;
        if (current_entry.d_type != DT_UNKNOWN) {
            bc_io_file_dtype_to_entry_type(current_entry.d_type, &entry_type);
        } else {
            time_t modification_time_value = 0;
            if (!bc_io_file_stat_if_unknown(directory_file_descriptor, current_entry.name, &entry_type, &resolved_device_id,
                                            &resolved_inode_number, &resolved_file_size, &modification_time_value)) {
                bc_io_walk_sequential_report_error(state, child_path_buffer, "stat", errno);
                continue;
            }
            resolved_modification_time = (long)modification_time_value;
            stat_populated = true;
        }

        bc_io_walk_entry_kind_t entry_kind = BC_IO_WALK_ENTRY_OTHER;
        bc_io_walk_sequential_entry_kind_from_type(entry_type, &entry_kind);

        bool needs_stat = !stat_populated && (entry_kind == BC_IO_WALK_ENTRY_FILE || entry_kind == BC_IO_WALK_ENTRY_DIRECTORY ||
                                              (entry_kind == BC_IO_WALK_ENTRY_SYMLINK && state->config->follow_symlinks));
        if (needs_stat) {
            int stat_flags = state->config->follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
            struct stat stat_buffer;
            if (fstatat(directory_file_descriptor, current_entry.name, &stat_buffer, stat_flags) != 0) {
                bc_io_walk_sequential_report_error(state, child_path_buffer, "stat", errno);
                continue;
            }
            resolved_file_size = (size_t)stat_buffer.st_size;
            resolved_device_id = stat_buffer.st_dev;
            resolved_inode_number = stat_buffer.st_ino;
            resolved_modification_time = (long)stat_buffer.st_mtime;
            resolved_permission_mask = (unsigned int)(stat_buffer.st_mode & 07777);
            if (entry_kind == BC_IO_WALK_ENTRY_SYMLINK && state->config->follow_symlinks) {
                if (S_ISREG(stat_buffer.st_mode)) {
                    entry_kind = BC_IO_WALK_ENTRY_FILE;
                } else if (S_ISDIR(stat_buffer.st_mode)) {
                    entry_kind = BC_IO_WALK_ENTRY_DIRECTORY;
                }
            }
            stat_populated = true;
        }

        bc_io_walk_entry_t walk_entry = {
            .absolute_path = child_path_buffer,
            .absolute_path_length = child_path_length,
            .kind = entry_kind,
            .file_size = resolved_file_size,
            .depth = directory_depth + 1U,
            .device_id = (unsigned long long)resolved_device_id,
            .inode_number = (unsigned long long)resolved_inode_number,
            .modification_time_seconds = resolved_modification_time,
            .permission_mask = resolved_permission_mask,
            .stat_populated = stat_populated,
        };

        bool accepted = true;
        if (state->config->filter != NULL) {
            accepted = state->config->filter(&walk_entry, state->config->filter_user_data);
        }
        if (!accepted) {
            state->stats.files_skipped += 1;
            continue;
        }

        if (entry_kind == BC_IO_WALK_ENTRY_FILE) {
            state->stats.files_visited += 1;
            if (!state->config->visit(&walk_entry, state->config->visit_user_data)) {
                state->visit_failed = true;
                break;
            }
        } else if (entry_kind == BC_IO_WALK_ENTRY_DIRECTORY) {
            if (state->config->visit != NULL) {
                if (!state->config->visit(&walk_entry, state->config->visit_user_data)) {
                    state->visit_failed = true;
                    break;
                }
            }

            bool descend = true;
            if (state->config->should_descend != NULL) {
                descend = state->config->should_descend(&walk_entry, state->config->should_descend_user_data);
            }
            if (!descend) {
                continue;
            }
            bc_io_walk_sequential_recurse(state, child_path_buffer, child_path_length, walk_entry.depth);
        }
    }

    bc_io_dirent_reader_destroy(state->config->main_memory_context, dirent_reader);
    close(directory_file_descriptor);
}

bool bc_io_walk_sequential(const bc_io_walk_config_t* config, bc_io_walk_stats_t* out_stats)
{
    if (config == NULL || config->root == NULL || config->root_length == 0 || config->root_length >= BC_IO_MAX_PATH_LENGTH) {
        return false;
    }
    if (config->visit == NULL) {
        return false;
    }
    if (config->main_memory_context == NULL) {
        return false;
    }

    bc_io_walk_sequential_state_t state;
    bc_core_zero(&state, sizeof(state));
    state.config = config;

    bc_io_walk_sequential_recurse(&state, config->root, config->root_length, 0U);

    if (out_stats != NULL) {
        *out_stats = state.stats;
    }
    if (state.visit_failed) {
        return false;
    }
    return true;
}
