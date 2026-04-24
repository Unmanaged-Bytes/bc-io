// SPDX-License-Identifier: MIT

#ifndef BC_IO_WALK_H
#define BC_IO_WALK_H

#include "bc_allocators.h"
#include "bc_concurrency.h"
#include "bc_concurrency_signal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    BC_IO_WALK_ENTRY_FILE,
    BC_IO_WALK_ENTRY_DIRECTORY,
    BC_IO_WALK_ENTRY_SYMLINK,
    BC_IO_WALK_ENTRY_OTHER,
} bc_io_walk_entry_kind_t;

typedef struct bc_io_walk_entry {
    const char* absolute_path;
    size_t absolute_path_length;
    bc_io_walk_entry_kind_t kind;
    size_t file_size;
    dev_t device_id;
    ino_t inode_number;
} bc_io_walk_entry_t;

typedef bool (*bc_io_walk_filter_fn)(const bc_io_walk_entry_t* entry, void* user_data);
typedef bool (*bc_io_walk_visit_fn)(const bc_io_walk_entry_t* entry, void* user_data);
typedef void (*bc_io_walk_error_fn)(const char* path, const char* stage, int errno_value, void* user_data);

typedef struct bc_io_walk_config {
    const char* root;
    size_t root_length;

    bc_allocators_context_t* main_memory_context;
    bc_concurrency_context_t* concurrency_context;
    bc_concurrency_signal_handler_t* signal_handler;

    size_t queue_capacity;

    bc_io_walk_filter_fn filter;
    void* filter_user_data;

    bc_io_walk_visit_fn visit;
    void* visit_user_data;

    bc_io_walk_error_fn on_error;
    void* error_user_data;
} bc_io_walk_config_t;

typedef struct bc_io_walk_stats {
    size_t files_visited;
    size_t directories_visited;
    size_t files_skipped;
    size_t errors_encountered;
} bc_io_walk_stats_t;

bool bc_io_walk_parallel(const bc_io_walk_config_t* config, bc_io_walk_stats_t* out_stats);

#endif /* BC_IO_WALK_H */
