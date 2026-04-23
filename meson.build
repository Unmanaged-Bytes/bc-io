# SPDX-License-Identifier: MIT

project('bc-io', 'c',
  version : '1.1.1',
  license : 'MIT',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
    'optimization=3',
    'default_library=static',
  ],
)

cc = meson.get_compiler('c')

extra_c_args = [
  '-pedantic',
  '-Wconversion',
  '-Wshadow',
  '-Wformat=2',
]

extra_c_args += '-D_GNU_SOURCE'

foreach arg : extra_c_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

lto_c_args = []
lto_link_args = []
if get_option('buildtype') in ['release', 'minsize']
  arch_value = get_option('arch')
  if arch_value == ''
    arch_value = 'x86-64-v3'
  endif

  add_project_arguments(
    '-march=' + arch_value,
    '-funroll-loops',
    '-fomit-frame-pointer',
    '-ffunction-sections',
    '-fdata-sections',
    language : 'c',
  )
  add_project_link_arguments('-Wl,--gc-sections', language : 'c')
endif

pkg = import('pkgconfig')

bc_core_dep = dependency('bc-core')
bc_allocators_dep = dependency('bc-allocators')

bc_io_public_inc = include_directories('include/public')
bc_io_internal_inc = include_directories('include/internal')

subdir('src')

pkg.generate(
  bc_io_lib,
  name : 'bc-io',
  description : 'Unified I/O : streams, filesystem helpers, mmap',
  version : meson.project_version(),
  extra_cflags : ['-I${includedir}/bc'],
)

install_headers(
  'include/public/bc_io.h',
  'include/public/bc_io_dirent_reader.h',
  'include/public/bc_io_file.h',
  'include/public/bc_io_file_inode.h',
  'include/public/bc_io_file_open.h',
  'include/public/bc_io_file_path.h',
  'include/public/bc_io_mmap.h',
  'include/public/bc_io_stream.h',
  subdir : 'bc',
)

if get_option('tests')
  subdir('tests')
endif

if get_option('benchmarks')
  subdir('benchmarks')
endif

if get_option('fuzzing')
  subdir('fuzzing')
endif
