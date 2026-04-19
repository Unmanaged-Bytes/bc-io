bc_io_lib = static_library('bc-io',
    'stream/bc_io_stream.c',
    'stream/bc_io_stream_read.c',
    'stream/bc_io_stream_write.c',
    'stream/bc_io_stream_io.c',
    'file/bc_io_file_open.c',
    'file/bc_io_file_path.c',
    'file/bc_io_file_inode.c',
    'mmap/bc_io_mmap.c',
    include_directories : [bc_io_public_inc, bc_io_internal_inc],
    dependencies : [bc_core_dep, bc_allocators_dep],
    c_args : lto_c_args,
    link_args : lto_link_args,
    install : true,
)

bc_io_dep = declare_dependency(
    link_with : bc_io_lib,
    include_directories : bc_io_public_inc,
    dependencies : [bc_core_dep, bc_allocators_dep],
)
