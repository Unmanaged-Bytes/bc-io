# Changelog

All notable changes to bc-io are documented here.

## [1.0.0]

Initial public release.

### Added

- **File primitives** (`bc_io_file.h`, `bc_io_file_open.h`,
  `bc_io_file_inode.h`, `bc_io_file_path.h`): open/close wrappers,
  path helpers, inode retrieval.
- **Memory-mapped file** (`bc_io_mmap.h`): mmap / munmap helpers
  with length and flag handling.
- **Buffered stream** (`bc_io_stream.h`): buffered read/write
  stream with explicit `advise()` hints (readahead / sequential /
  random).

### Quality

- Unit tests under `tests/`, built with cmocka.
- Sanitizers (asan / tsan / ubsan / memcheck) pass.
- cppcheck clean on the project sources.
- MIT-licensed, static `.a` published as Debian `.deb` via GitHub
  Releases.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-io/releases/tag/v1.0.0
