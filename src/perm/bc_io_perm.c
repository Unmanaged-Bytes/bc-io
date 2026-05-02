// SPDX-License-Identifier: MIT

#include "bc_io_perm.h"

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

static char bc_io_perm_file_type_char(uint32_t mode)
{
    if (S_ISREG(mode)) {
        return '-';
    }
    if (S_ISDIR(mode)) {
        return 'd';
    }
    if (S_ISLNK(mode)) {
        return 'l';
    }
    if (S_ISCHR(mode)) {
        return 'c';
    }
    if (S_ISBLK(mode)) {
        return 'b';
    }
    if (S_ISFIFO(mode)) {
        return 'p';
    }
    if (S_ISSOCK(mode)) {
        return 's';
    }
    return '?';
}

bool bc_io_perm_format_mode_human(uint32_t mode, char* output, size_t capacity, size_t* out_length)
{
    if (capacity < 10u) {
        return false;
    }
    output[0] = bc_io_perm_file_type_char(mode);
    output[1] = (mode & S_IRUSR) ? 'r' : '-';
    output[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID) {
        output[3] = (mode & S_IXUSR) ? 's' : 'S';
    } else {
        output[3] = (mode & S_IXUSR) ? 'x' : '-';
    }
    output[4] = (mode & S_IRGRP) ? 'r' : '-';
    output[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID) {
        output[6] = (mode & S_IXGRP) ? 's' : 'S';
    } else {
        output[6] = (mode & S_IXGRP) ? 'x' : '-';
    }
    output[7] = (mode & S_IROTH) ? 'r' : '-';
    output[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX) {
        output[9] = (mode & S_IXOTH) ? 't' : 'T';
    } else {
        output[9] = (mode & S_IXOTH) ? 'x' : '-';
    }
    *out_length = 10u;
    return true;
}

static bool bc_io_perm_parse_rwx_triple(const char* triple, uint32_t read_bit, uint32_t write_bit, uint32_t exec_bit, uint32_t special_bit,
                                        char special_with_exec, char special_no_exec, uint32_t* accumulated)
{
    if (triple[0] != 'r' && triple[0] != '-') {
        return false;
    }
    if (triple[1] != 'w' && triple[1] != '-') {
        return false;
    }
    if (triple[0] == 'r') {
        *accumulated |= read_bit;
    }
    if (triple[1] == 'w') {
        *accumulated |= write_bit;
    }
    const char execute_char = triple[2];
    if (execute_char == 'x') {
        *accumulated |= exec_bit;
    } else if (execute_char == '-') {
        /* no execute */
    } else if (execute_char == special_with_exec) {
        *accumulated |= exec_bit;
        *accumulated |= special_bit;
    } else if (execute_char == special_no_exec) {
        *accumulated |= special_bit;
    } else {
        return false;
    }
    return true;
}

bool bc_io_perm_parse_mode_human(const char* input, size_t length, uint32_t* out_mode)
{
    if (length != 10u && length != 9u) {
        return false;
    }
    if (length == 10u) {
        const char type_char = input[0];
        if (type_char != '-' && type_char != 'd' && type_char != 'l' && type_char != 'c' && type_char != 'b' && type_char != 'p' &&
            type_char != 's' && type_char != '?') {
            return false;
        }
    }
    const size_t base = (length == 10u) ? 1u : 0u;
    uint32_t mode = 0;
    if (!bc_io_perm_parse_rwx_triple(&input[base], S_IRUSR, S_IWUSR, S_IXUSR, S_ISUID, 's', 'S', &mode)) {
        return false;
    }
    if (!bc_io_perm_parse_rwx_triple(&input[base + 3u], S_IRGRP, S_IWGRP, S_IXGRP, S_ISGID, 's', 'S', &mode)) {
        return false;
    }
    if (!bc_io_perm_parse_rwx_triple(&input[base + 6u], S_IROTH, S_IWOTH, S_IXOTH, S_ISVTX, 't', 'T', &mode)) {
        return false;
    }
    *out_mode = mode;
    return true;
}

bool bc_io_perm_format_mode_octal(uint32_t mode, char* output, size_t capacity, size_t* out_length)
{
    if (capacity < 4u) {
        return false;
    }
    const uint32_t special = (mode & (S_ISUID | S_ISGID | S_ISVTX)) >> 9;
    output[0] = (char)('0' + (special & 7u));
    output[1] = (char)('0' + ((mode >> 6) & 7u));
    output[2] = (char)('0' + ((mode >> 3) & 7u));
    output[3] = (char)('0' + (mode & 7u));
    *out_length = 4u;
    return true;
}

bool bc_io_perm_parse_mode_octal(const char* input, size_t length, uint32_t* out_mode)
{
    if (length == 0u || length > 4u) {
        return false;
    }
    uint32_t result = 0;
    for (size_t index = 0; index < length; index++) {
        const char c = input[index];
        if (c < '0' || c > '7') {
            return false;
        }
        result = (result << 3) | (uint32_t)(c - '0');
    }
    if (length == 4u) {
        const uint32_t special = (result >> 9) & 7u;
        uint32_t mode = result & 0x1FFu;
        if (special & 4u) {
            mode |= S_ISUID;
        }
        if (special & 2u) {
            mode |= S_ISGID;
        }
        if (special & 1u) {
            mode |= S_ISVTX;
        }
        *out_mode = mode;
    } else {
        *out_mode = result;
    }
    return true;
}

static bool bc_io_perm_copy_string(const char* source, char* output, size_t capacity, size_t* out_length)
{
    size_t length = 0;
    while (source[length] != '\0') {
        length += 1;
    }
    if (length > capacity) {
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        output[index] = source[index];
    }
    *out_length = length;
    return true;
}

bool bc_io_perm_resolve_user_name(uint32_t user_id, char* output, size_t capacity, size_t* out_length)
{
    char buffer[1024];
    struct passwd password_entry;
    struct passwd* result = NULL;
    if (getpwuid_r((uid_t)user_id, &password_entry, buffer, sizeof(buffer), &result) != 0) {
        return false;
    }
    if (result == NULL) {
        return false;
    }
    return bc_io_perm_copy_string(result->pw_name, output, capacity, out_length);
}

bool bc_io_perm_resolve_group_name(uint32_t group_id, char* output, size_t capacity, size_t* out_length)
{
    char buffer[1024];
    struct group group_entry;
    struct group* result = NULL;
    if (getgrgid_r((gid_t)group_id, &group_entry, buffer, sizeof(buffer), &result) != 0) {
        return false;
    }
    if (result == NULL) {
        return false;
    }
    return bc_io_perm_copy_string(result->gr_name, output, capacity, out_length);
}

static bool bc_io_perm_copy_to_zero_terminated(const char* source, size_t length, char* output, size_t capacity)
{
    if (length + 1u > capacity) {
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        output[index] = source[index];
    }
    output[length] = '\0';
    return true;
}

bool bc_io_perm_resolve_user_id(const char* name, size_t length, uint32_t* out_user_id)
{
    char zero_terminated[256];
    if (!bc_io_perm_copy_to_zero_terminated(name, length, zero_terminated, sizeof(zero_terminated))) {
        return false;
    }
    char buffer[1024];
    struct passwd password_entry;
    struct passwd* result = NULL;
    if (getpwnam_r(zero_terminated, &password_entry, buffer, sizeof(buffer), &result) != 0) {
        return false;
    }
    if (result == NULL) {
        return false;
    }
    *out_user_id = (uint32_t)result->pw_uid;
    return true;
}

bool bc_io_perm_resolve_group_id(const char* name, size_t length, uint32_t* out_group_id)
{
    char zero_terminated[256];
    if (!bc_io_perm_copy_to_zero_terminated(name, length, zero_terminated, sizeof(zero_terminated))) {
        return false;
    }
    char buffer[1024];
    struct group group_entry;
    struct group* result = NULL;
    if (getgrnam_r(zero_terminated, &group_entry, buffer, sizeof(buffer), &result) != 0) {
        return false;
    }
    if (result == NULL) {
        return false;
    }
    *out_group_id = (uint32_t)result->gr_gid;
    return true;
}
