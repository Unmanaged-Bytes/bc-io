// SPDX-License-Identifier: MIT

#ifndef BC_IO_PERM_H
#define BC_IO_PERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool bc_io_perm_format_mode_human(uint32_t mode, char* output, size_t capacity, size_t* out_length);
bool bc_io_perm_parse_mode_human(const char* input, size_t length, uint32_t* out_mode);

bool bc_io_perm_format_mode_octal(uint32_t mode, char* output, size_t capacity, size_t* out_length);
bool bc_io_perm_parse_mode_octal(const char* input, size_t length, uint32_t* out_mode);

bool bc_io_perm_resolve_user_name(uint32_t user_id, char* output, size_t capacity, size_t* out_length);
bool bc_io_perm_resolve_group_name(uint32_t group_id, char* output, size_t capacity, size_t* out_length);
bool bc_io_perm_resolve_user_id(const char* name, size_t length, uint32_t* out_user_id);
bool bc_io_perm_resolve_group_id(const char* name, size_t length, uint32_t* out_group_id);

#endif /* BC_IO_PERM_H */
