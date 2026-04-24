// SPDX-License-Identifier: MIT

#include "bc_io_walk.h"

#include "bc_io_dirent_reader.h"
#include "bc_io_file.h"
#include "bc_io_file_path.h"

#include "bc_concurrency.h"
#include "bc_concurrency_signal.h"
#include "bc_core.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#define BC_IO_WALK_DEFAULT_QUEUE_CAPACITY ((size_t)16384)
#define BC_IO_WALK_TERMINATION_SPIN_PAUSES ((int)64)

typedef struct bc_io_walk_queue_entry {
    char absolute_path[BC_IO_MAX_PATH_LENGTH];
    size_t absolute_path_length;
} bc_io_walk_queue_entry_t;

typedef struct bc_io_walk_shared {
    const bc_io_walk_config_t* config;
    bc_concurrency_queue_t* directory_queue;
    _Atomic int outstanding_directory_count;
    _Atomic size_t files_visited;
    _Atomic size_t directories_visited;
    _Atomic size_t files_skipped;
    _Atomic size_t errors_encountered;
    _Atomic int visit_failed;
} bc_io_walk_shared_t;

static bool bc_io_walk_should_stop(const bc_io_walk_shared_t* shared)
{
    if (shared->config->signal_handler == NULL) {
        return false;
    }
    bool should_stop = false;
    bc_concurrency_signal_handler_should_stop(shared->config->signal_handler, &should_stop);
    return should_stop;
}

static void bc_io_walk_report_error(bc_io_walk_shared_t* shared, const char* path, const char* stage, int errno_value)
{
    atomic_fetch_add_explicit(&shared->errors_encountered, 1, memory_order_relaxed);
    if (shared->config->on_error != NULL) {
        shared->config->on_error(path, stage, errno_value, shared->config->error_user_data);
    }
}

static bool bc_io_walk_entry_passes_filter(bc_io_walk_shared_t* shared, const bc_io_walk_entry_t* entry)
{
    if (shared->config->filter == NULL) {
        return true;
    }
    return shared->config->filter(entry, shared->config->filter_user_data);
}

static bool bc_io_walk_entry_kind_from_type(bc_io_file_entry_type_t type, bc_io_walk_entry_kind_t* out_kind)
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

static void bc_io_walk_process_directory(bc_io_walk_shared_t* shared, const char* directory_path, size_t directory_path_length)
{
    if (atomic_load_explicit(&shared->visit_failed, memory_order_relaxed)) {
        return;
    }

    int directory_file_descriptor = open(directory_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory_file_descriptor < 0) {
        bc_io_walk_report_error(shared, directory_path, "open", errno);
        return;
    }

    atomic_fetch_add_explicit(&shared->directories_visited, 1, memory_order_relaxed);

    bc_io_dirent_reader_t dirent_reader;
    bc_io_dirent_reader_init(&dirent_reader, directory_file_descriptor);

    for (;;) {
        if (atomic_load_explicit(&shared->visit_failed, memory_order_relaxed)) {
            break;
        }

        bc_io_dirent_entry_t current_entry;
        bool has_entry = false;
        if (!bc_io_dirent_reader_next(&dirent_reader, &current_entry, &has_entry)) {
            bc_io_walk_report_error(shared, directory_path, "getdents64", dirent_reader.last_errno);
            break;
        }
        if (!has_entry) {
            break;
        }
        if (current_entry.name[0] == '.') {
            continue;
        }

        char child_path_buffer[BC_IO_MAX_PATH_LENGTH];
        size_t child_path_length = 0;
        if (!bc_io_file_path_join(child_path_buffer, sizeof(child_path_buffer), directory_path, directory_path_length, current_entry.name,
                                  current_entry.name_length, &child_path_length)) {
            bc_io_walk_report_error(shared, directory_path, "path-too-long", ENAMETOOLONG);
            continue;
        }

        bc_io_file_entry_type_t entry_type = BC_IO_ENTRY_TYPE_OTHER;
        size_t resolved_file_size = 0;
        dev_t resolved_device_id = 0;
        ino_t resolved_inode_number = 0;
        bool metadata_already_known = false;
        if (current_entry.d_type != DT_UNKNOWN) {
            bc_io_file_dtype_to_entry_type(current_entry.d_type, &entry_type);
        } else {
            time_t modification_time_value = 0;
            if (!bc_io_file_stat_if_unknown(directory_file_descriptor, current_entry.name, &entry_type, &resolved_device_id, &resolved_inode_number,
                                            &resolved_file_size, &modification_time_value)) {
                bc_io_walk_report_error(shared, child_path_buffer, "stat", errno);
                continue;
            }
            metadata_already_known = true;
        }

        bc_io_walk_entry_kind_t entry_kind = BC_IO_WALK_ENTRY_OTHER;
        bc_io_walk_entry_kind_from_type(entry_type, &entry_kind);

        if ((entry_kind == BC_IO_WALK_ENTRY_FILE || entry_kind == BC_IO_WALK_ENTRY_DIRECTORY) && !metadata_already_known) {
            struct stat stat_buffer;
            if (fstatat(directory_file_descriptor, current_entry.name, &stat_buffer, AT_SYMLINK_NOFOLLOW) != 0) {
                bc_io_walk_report_error(shared, child_path_buffer, "stat", errno);
                continue;
            }
            resolved_file_size = (size_t)stat_buffer.st_size;
            resolved_device_id = stat_buffer.st_dev;
            resolved_inode_number = stat_buffer.st_ino;
        }

        bc_io_walk_entry_t walk_entry = {
            .absolute_path = child_path_buffer,
            .absolute_path_length = child_path_length,
            .kind = entry_kind,
            .file_size = resolved_file_size,
            .device_id = resolved_device_id,
            .inode_number = resolved_inode_number,
        };

        bool accepted = bc_io_walk_entry_passes_filter(shared, &walk_entry);
        if (!accepted) {
            atomic_fetch_add_explicit(&shared->files_skipped, 1, memory_order_relaxed);
            continue;
        }

        if (entry_kind == BC_IO_WALK_ENTRY_FILE) {
            atomic_fetch_add_explicit(&shared->files_visited, 1, memory_order_relaxed);
            if (!shared->config->visit(&walk_entry, shared->config->visit_user_data)) {
                atomic_store_explicit(&shared->visit_failed, 1, memory_order_relaxed);
                break;
            }
        } else if (entry_kind == BC_IO_WALK_ENTRY_DIRECTORY) {
            if (shared->config->visit != NULL) {
                if (!shared->config->visit(&walk_entry, shared->config->visit_user_data)) {
                    atomic_store_explicit(&shared->visit_failed, 1, memory_order_relaxed);
                    break;
                }
            }

            bc_io_walk_queue_entry_t sub_entry;
            bc_core_zero(&sub_entry, sizeof(sub_entry));
            bc_core_copy(sub_entry.absolute_path, child_path_buffer, child_path_length);
            sub_entry.absolute_path_length = child_path_length;
            sub_entry.absolute_path[child_path_length] = '\0';

            atomic_fetch_add_explicit(&shared->outstanding_directory_count, 1, memory_order_relaxed);
            if (!bc_concurrency_queue_push(shared->directory_queue, &sub_entry)) {
                atomic_fetch_sub_explicit(&shared->outstanding_directory_count, 1, memory_order_relaxed);
                bc_io_walk_process_directory(shared, sub_entry.absolute_path, sub_entry.absolute_path_length);
            }
        }
    }

    close(directory_file_descriptor);
}

static void bc_io_walk_worker_task(void* task_argument)
{
    bc_io_walk_shared_t* shared = (bc_io_walk_shared_t*)task_argument;

    for (;;) {
        if (bc_io_walk_should_stop(shared)) {
            return;
        }
        if (atomic_load_explicit(&shared->visit_failed, memory_order_relaxed)) {
            return;
        }

        bc_io_walk_queue_entry_t entry;
        if (bc_concurrency_queue_pop(shared->directory_queue, &entry)) {
            bc_io_walk_process_directory(shared, entry.absolute_path, entry.absolute_path_length);
            atomic_fetch_sub_explicit(&shared->outstanding_directory_count, 1, memory_order_release);
            continue;
        }
        int outstanding = atomic_load_explicit(&shared->outstanding_directory_count, memory_order_acquire);
        if (outstanding == 0) {
            return;
        }
        for (int spin = 0; spin < BC_IO_WALK_TERMINATION_SPIN_PAUSES; ++spin) {
            bc_core_spin_pause();
        }
    }
}

bool bc_io_walk_parallel(const bc_io_walk_config_t* config, bc_io_walk_stats_t* out_stats)
{
    if (config->root == NULL || config->root_length == 0 || config->root_length >= BC_IO_MAX_PATH_LENGTH) {
        return false;
    }
    if (config->visit == NULL) {
        return false;
    }
    if (config->main_memory_context == NULL || config->concurrency_context == NULL) {
        return false;
    }

    size_t queue_capacity = config->queue_capacity > 0 ? config->queue_capacity : BC_IO_WALK_DEFAULT_QUEUE_CAPACITY;

    bc_io_walk_shared_t shared;
    bc_core_zero(&shared, sizeof(shared));
    shared.config = config;
    atomic_store_explicit(&shared.outstanding_directory_count, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.files_visited, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.directories_visited, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.files_skipped, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.errors_encountered, 0, memory_order_relaxed);
    atomic_store_explicit(&shared.visit_failed, 0, memory_order_relaxed);

    if (!bc_concurrency_queue_create(config->main_memory_context, sizeof(bc_io_walk_queue_entry_t), queue_capacity, &shared.directory_queue)) {
        return false;
    }

    bc_io_walk_queue_entry_t root_entry;
    bc_core_zero(&root_entry, sizeof(root_entry));
    bc_core_copy(root_entry.absolute_path, config->root, config->root_length);
    root_entry.absolute_path_length = config->root_length;
    root_entry.absolute_path[config->root_length] = '\0';

    atomic_store_explicit(&shared.outstanding_directory_count, 1, memory_order_relaxed);
    if (!bc_concurrency_queue_push(shared.directory_queue, &root_entry)) {
        bc_concurrency_queue_destroy(shared.directory_queue);
        return false;
    }

    size_t effective_worker_count = bc_concurrency_effective_worker_count(config->concurrency_context);
    for (size_t worker_index = 0; worker_index < effective_worker_count; ++worker_index) {
        bc_concurrency_submit(config->concurrency_context, bc_io_walk_worker_task, &shared);
    }
    bool dispatch_ok = bc_concurrency_dispatch_and_wait(config->concurrency_context);

    bc_concurrency_queue_destroy(shared.directory_queue);

    if (out_stats != NULL) {
        out_stats->files_visited = atomic_load_explicit(&shared.files_visited, memory_order_relaxed);
        out_stats->directories_visited = atomic_load_explicit(&shared.directories_visited, memory_order_relaxed);
        out_stats->files_skipped = atomic_load_explicit(&shared.files_skipped, memory_order_relaxed);
        out_stats->errors_encountered = atomic_load_explicit(&shared.errors_encountered, memory_order_relaxed);
    }

    if (!dispatch_ok) {
        return false;
    }
    if (atomic_load_explicit(&shared.visit_failed, memory_order_relaxed)) {
        return false;
    }
    return true;
}
