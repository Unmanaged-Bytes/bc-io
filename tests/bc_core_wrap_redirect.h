// SPDX-License-Identifier: MIT

#ifndef BC_IO_TESTS_BC_CORE_WRAP_REDIRECT_H
#define BC_IO_TESTS_BC_CORE_WRAP_REDIRECT_H

#include <stdbool.h>
#include <stddef.h>

#define bc_core_safe_add bc_core_safe_add_inline_unused
#define bc_core_safe_multiply bc_core_safe_multiply_inline_unused

#include "bc_core.h"

#undef bc_core_safe_add
#undef bc_core_safe_multiply

extern bool __wrap_bc_core_safe_add(size_t a, size_t b, size_t* out_result);
extern bool __wrap_bc_core_safe_multiply(size_t a, size_t b, size_t* out_result);

#define __real_bc_core_safe_add bc_core_safe_add_inline_unused
#define __real_bc_core_safe_multiply bc_core_safe_multiply_inline_unused

#define bc_core_safe_add __wrap_bc_core_safe_add
#define bc_core_safe_multiply __wrap_bc_core_safe_multiply

#endif /* BC_IO_TESTS_BC_CORE_WRAP_REDIRECT_H */
