# SPDX-License-Identifier: MIT

fuzzer_c_args = []
fuzzer_link_args = []
if cc.get_id() == 'clang'
    fuzzer_c_args += ['-fsanitize=fuzzer', '-DBC_FUZZ_LIBFUZZER']
    fuzzer_link_args += ['-fsanitize=fuzzer']
endif

fuzz_targets = [
    'bc_io_stream',
    'bc_io_mmap',
    'bc_io_file_path_join',
    'bc_io_file_inode_set',
]

foreach name : fuzz_targets
    executable('fuzz_' + name,
        'fuzz_' + name + '.c',
        dependencies : [bc_io_dep],
        c_args : fuzzer_c_args,
        link_args : fuzzer_link_args,
    )
endforeach
