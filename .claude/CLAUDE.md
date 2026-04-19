# bc-io — project context

I/O primitives for the `bc-*` ecosystem: open/close file wrappers,
path / inode helpers, mmap, and a buffered stream with explicit
advise hints. Thin Linux-specific layer above libc syscalls.

## Invariants (do not break)

- **No comments in `.c` files** — code names itself. Public `.h`
  may carry one-line contracts if the signature is insufficient.
- **No defensive null-checks at function entry.** Return `false`
  on legitimate failure; never assert in production paths.
- **SPDX-License-Identifier: MIT** header on every `.c` and `.h`.
- **Strict C11** with `-Wall -Wextra -Wpedantic -Werror`.
- **Sanitizers (asan/tsan/ubsan/memcheck) stay green** in CI.
- **cppcheck stays clean**; never edit `cppcheck-suppressions.txt`
  to hide real findings.
- **Linux-only** — relies on `stat`, `open`, `mmap`, `posix_fadvise`.
