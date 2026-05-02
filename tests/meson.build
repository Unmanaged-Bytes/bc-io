cmocka_dep = dependency('cmocka')

stream_dir = '../src/stream'
file_dir = '../src/file'
mmap_dir = '../src/mmap'

# === Stream --wrap tests (recompile sources, inject syscall failures) ===

test_bc_io_stream_io_wrap = executable('test_bc_io_stream_io_wrap',
    'test_bc_io_stream_io_wrap.c',
    stream_dir + '/bc_io_stream_io.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [cmocka_dep, bc_allocators_dep, bc_core_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=read',
        '-Wl,--wrap=write',
    ],
)
test('bc_io_stream_io_wrap', test_bc_io_stream_io_wrap)

test_bc_io_stream_open_wrap = executable('test_bc_io_stream_open_wrap',
    'test_bc_io_stream_open_wrap.c',
    stream_dir + '/bc_io_stream.c',
    stream_dir + '/bc_io_stream_read.c',
    stream_dir + '/bc_io_stream_write.c',
    stream_dir + '/bc_io_stream_io.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [cmocka_dep, bc_allocators_dep, bc_core_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=open64',
        '-Wl,--wrap=fstat64',
        '-Wl,--wrap=posix_fadvise64',
    ],
)
test('bc_io_stream_open_wrap', test_bc_io_stream_open_wrap)

test_bc_io_stream_allocation_wrap = executable('test_bc_io_stream_allocation_wrap',
    'test_bc_io_stream_allocation_wrap.c',
    stream_dir + '/bc_io_stream.c',
    stream_dir + '/bc_io_stream_read.c',
    stream_dir + '/bc_io_stream_write.c',
    stream_dir + '/bc_io_stream_io.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [cmocka_dep, bc_allocators_dep, bc_core_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_allocators_arena_create',
        '-Wl,--wrap=bc_allocators_arena_allocate',
    ],
)
test('bc_io_stream_allocation_wrap', test_bc_io_stream_allocation_wrap)

test_bc_io_stream_write_wrap = executable('test_bc_io_stream_write_wrap',
    'test_bc_io_stream_write_wrap.c',
    stream_dir + '/bc_io_stream.c',
    stream_dir + '/bc_io_stream_read.c',
    stream_dir + '/bc_io_stream_write.c',
    stream_dir + '/bc_io_stream_io.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [cmocka_dep, bc_allocators_dep, bc_core_dep],
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=write',
    ],
)
test('bc_io_stream_write_wrap', test_bc_io_stream_write_wrap)

test_bc_io_stream_write_pipe = executable('test_bc_io_stream_write_pipe',
    'test_bc_io_stream_write_pipe.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_write_pipe', test_bc_io_stream_write_pipe)

test_bc_io_stream_alignment = executable('test_bc_io_stream_alignment',
    'test_bc_io_stream_alignment.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_alignment', test_bc_io_stream_alignment)

# === Stream integration tests ===

test_bc_io_stream_write_file = executable('test_bc_io_stream_write_file',
    'test_bc_io_stream_write_file.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_write_file', test_bc_io_stream_write_file)

test_bc_io_stream_read_file = executable('test_bc_io_stream_read_file',
    'test_bc_io_stream_read_file.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_read_file', test_bc_io_stream_read_file)

test_bc_io_stream_read_memory = executable('test_bc_io_stream_read_memory',
    'test_bc_io_stream_read_memory.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_read_memory', test_bc_io_stream_read_memory)

test_bc_io_stream_read_pipe = executable('test_bc_io_stream_read_pipe',
    'test_bc_io_stream_read_pipe.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_read_pipe', test_bc_io_stream_read_pipe)

test_bc_io_stream_read_socket = executable('test_bc_io_stream_read_socket',
    'test_bc_io_stream_read_socket.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_read_socket', test_bc_io_stream_read_socket)

test_bc_io_stream_metadata = executable('test_bc_io_stream_metadata',
    'test_bc_io_stream_metadata.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_metadata', test_bc_io_stream_metadata)

test_bc_io_stream_stats = executable('test_bc_io_stream_stats',
    'test_bc_io_stream_stats.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_stats', test_bc_io_stream_stats)

test_bc_io_stream_edge_cases = executable('test_bc_io_stream_edge_cases',
    'test_bc_io_stream_edge_cases.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_edge_cases', test_bc_io_stream_edge_cases)

# === File / mmap tests ===

test_bc_io_file_inode_set_errors = executable('test_bc_io_file_inode_set_errors',
    'test_bc_io_file_inode_set_errors.c',
    file_dir + '/bc_io_file_inode.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [cmocka_dep, bc_allocators_dep, bc_core_dep],
    c_args : ['-fno-inline', '-include', meson.current_source_dir() / 'bc_core_wrap_redirect.h'],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_core_safe_multiply',
        '-Wl,--wrap=bc_core_safe_add',
    ],
)
test('bc_io_file_inode_set_errors', test_bc_io_file_inode_set_errors)

test_bc_io_file_inode_set = executable('test_bc_io_file_inode_set',
    'test_bc_io_file_inode_set.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_file_inode_set', test_bc_io_file_inode_set)

test_bc_io_file_map = executable('test_bc_io_file_map',
    'test_bc_io_file_map.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_file_map', test_bc_io_file_map)

test_bc_io_file_open = executable('test_bc_io_file_open',
    'test_bc_io_file_open.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_file_open', test_bc_io_file_open)

test_bc_io_file_helpers = executable('test_bc_io_file_helpers',
    'test_bc_io_file_helpers.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_file_helpers', test_bc_io_file_helpers)

test_bc_io_dirent_reader = executable('test_bc_io_dirent_reader',
    'test_bc_io_dirent_reader.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_dirent_reader', test_bc_io_dirent_reader)

test_bc_io_stream_advise = executable('test_bc_io_stream_advise',
    'test_bc_io_stream_advise.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_stream_advise', test_bc_io_stream_advise)

test_bc_io_symlink_safety = executable('test_bc_io_symlink_safety',
    'test_bc_io_symlink_safety.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_symlink_safety', test_bc_io_symlink_safety)

test_bc_io_umbrella_header_exposes_all_macros = executable('test_bc_io_umbrella_header_exposes_all_macros',
    'test_bc_io_umbrella_header_exposes_all_macros.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_umbrella_header_exposes_all_macros', test_bc_io_umbrella_header_exposes_all_macros)

test_bc_io_walk_parallel = executable('test_bc_io_walk_parallel',
    'test_bc_io_walk_parallel.c',
    dependencies : [cmocka_dep, bc_io_dep, bc_concurrency_dep],
)
test('bc_io_walk_parallel', test_bc_io_walk_parallel)

test_bc_io_walk_sequential = executable('test_bc_io_walk_sequential',
    'test_bc_io_walk_sequential.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_walk_sequential', test_bc_io_walk_sequential)

test_bc_io_random = executable('test_bc_io_random',
    'test_bc_io_random.c',
    dependencies : [cmocka_dep, bc_io_dep, dependency('threads')],
)
test('bc_io_random', test_bc_io_random)

test_bc_io_perm = executable('test_bc_io_perm',
    'test_bc_io_perm.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_perm', test_bc_io_perm)

test_bc_io_path = executable('test_bc_io_path',
    'test_bc_io_path.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_path', test_bc_io_path)

test_bc_io_file_stat = executable('test_bc_io_file_stat',
    'test_bc_io_file_stat.c',
    dependencies : [cmocka_dep, bc_io_dep],
)
test('bc_io_file_stat', test_bc_io_file_stat)
