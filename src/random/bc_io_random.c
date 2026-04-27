// SPDX-License-Identifier: MIT

#include "bc_io_random.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

static __thread int bc_io_random_urandom_descriptor = -1;

static bool bc_io_random_open_urandom(void)
{
    if (bc_io_random_urandom_descriptor >= 0) {
        return true;
    }
    int descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    bc_io_random_urandom_descriptor = descriptor;
    return true;
}

static bool bc_io_random_read_urandom(unsigned char* output, size_t length)
{
    if (!bc_io_random_open_urandom()) {
        return false;
    }
    size_t remaining = length;
    while (remaining > 0U) {
        ssize_t read_count = read(bc_io_random_urandom_descriptor, output, remaining);
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (read_count == 0) {
            return false;
        }
        output += (size_t)read_count;
        remaining -= (size_t)read_count;
    }
    return true;
}

bool bc_io_random_bytes(void* output, size_t length)
{
    if (length == 0U) {
        return true;
    }
    if (output == NULL) {
        return false;
    }

    unsigned char* buffer = (unsigned char*)output;
    size_t remaining = length;
    while (remaining > 0U) {
        ssize_t produced = getrandom(buffer, remaining, 0);
        if (produced < 0) {
            if (errno == EINTR) {
                continue;
            }
            return bc_io_random_read_urandom(buffer, remaining);
        }
        buffer += (size_t)produced;
        remaining -= (size_t)produced;
    }
    return true;
}

bool bc_io_random_unsigned_integer_64(uint64_t* out_value)
{
    if (out_value == NULL) {
        return false;
    }
    return bc_io_random_bytes(out_value, sizeof(*out_value));
}
