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


## Consommation du DAG bc-* (D063)

Cette lib consume les libs ascendantes du DAG. Elle doit utiliser
**exclusivement leur API** :

- **String/memory** : `bc_core` (equal/length/copy/zero/fill/find/contains).
  `<string.h>` direct interdit.
- **Format** : `bc_core_format` (decimal/hex/base64/hex_dump/bytes_human).
- **Allocations** : `bc_allocators` (pool/arena/slab/typed_array).
  `<stdlib.h>` malloc/free/realloc interdits.
- **Threading** : `bc_concurrency` (queue/context/tsan helpers/atomic).
  `<pthread.h>` direct interdit.
- **I/O** : `bc_io` (stream/file/walk/dirent_reader/mmap/perm).
- **CLI/runtime** : `bc_runtime` (cli/output/log/error_collector/lifecycle).

**Règle de propagation** : si une primitive manque dans une lib ascendante,
**l'ajouter là-bas d'abord** (composition R3 + R10), puis utiliser ici.
Pas de réimpl interne. Cette lib peut elle-même être consommée par des
libs en aval ou par des binaires `applications/bc-*/`.

Référence complète : `~/workspace/applications/CLAUDE.md` § 7,
`~/workspace/audits/DECISIONS.md` D063.
