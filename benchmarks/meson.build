benchmark('bench_bc_io_stream_read',
    executable('bench_bc_io_stream_read', 'bench_bc_io_stream_read.c',
        dependencies : [bc_io_dep],
    ),
    timeout : 120,
)

benchmark('bench_bc_io_stream_write',
    executable('bench_bc_io_stream_write', 'bench_bc_io_stream_write.c',
        dependencies : [bc_io_dep],
    ),
    timeout : 120,
)

benchmark('bench_bc_io_file_ops',
    executable('bench_bc_io_file_ops', 'bench_bc_io_file_ops.c',
        dependencies : [bc_io_dep],
    ),
    timeout : 120,
)

benchmark('bench_bc_io_vs_libc',
    executable('bench_bc_io_vs_libc', 'bench_bc_io_vs_libc.c',
        dependencies : [bc_io_dep],
    ),
    timeout : 600,
)
