"""Build static i386 GCC 16.2 and binutils 2.47 for the Barnix Linux ABI.

Requires host gcc/g++ -m32, 32-bit glibc development files, and the named
upstream source archives. Build output is deliberately outside the source tree.
"""
import argparse
import os
import pathlib
import shutil
import subprocess


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sources', type=pathlib.Path, required=True,
                    help='Directory with gcc, binutils, gmp, mpfr and mpc tar.xz files')
parser.add_argument('--work', type=pathlib.Path, required=True,
                    help='Build directory; allow several GiB of free space')
parser.add_argument('--jobs', type=int, default=4)
args = parser.parse_args()
sources = args.sources.resolve()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
source = work / 'src'
source.mkdir(exist_ok=True)
archives = {
    'gcc-16.2.0': 'gcc-16.2.0.tar.xz',
    'binutils-2.47': 'binutils-2.47.tar.xz',
    'gmp-6.3.0': 'gmp-6.3.0.tar.xz',
    'mpfr-4.2.2': 'mpfr-4.2.2.tar.xz',
    'mpc-1.4.1': 'mpc-1.4.1.tar.xz',
}
for directory, archive in archives.items():
    path = sources / archive
    if not path.is_file():
        parser.error(f'Missing source archive: {path}')
    if directory.startswith(('gmp-', 'mpfr-', 'mpc-')) and \
            (source / 'gcc-16.2.0' / directory.split('-')[0]).exists():
        continue
    if not (source / directory).exists():
        subprocess.run(['tar', '-xf', str(path), '-C', str(source)], check=True)
gcc_source = source / 'gcc-16.2.0'
for package in ('gmp', 'mpfr', 'mpc'):
    destination = gcc_source / package
    if not destination.exists():
        (source / next(name for name in archives if name.startswith(package + '-'))).rename(destination)

wrappers = work / 'bin'
wrappers.mkdir(exist_ok=True)
for name in ('gcc', 'g++'):
    wrapper = wrappers / f'i686-pc-linux-gnu-{name}'
    wrapper.write_text(f'#!/bin/sh\nexec {name} -m32 "$@"\n')
    wrapper.chmod(0o755)
for name in ('ar', 'as', 'ld', 'nm', 'objcopy', 'objdump', 'ranlib', 'readelf', 'strip'):
    link = wrappers / f'i686-pc-linux-gnu-{name}'
    if not link.exists():
        link.symlink_to(shutil.which(name))

env = os.environ.copy()
env.update(PATH=str(wrappers) + os.pathsep + env['PATH'],
           CC='gcc -m32', CXX='g++ -m32',
           CC_FOR_BUILD='gcc', CXX_FOR_BUILD='g++',
           CFLAGS_FOR_BUILD='-O2', CXXFLAGS_FOR_BUILD='-O2',
           LDFLAGS='-m32 -static -Wl,-Ttext-segment=0x80000000')
common = ['--build=x86_64-pc-linux-gnu', '--host=i686-pc-linux-gnu',
          '--target=i686-pc-linux-gnu', '--prefix=/usr',
          '--disable-nls', '--disable-shared']


def configure(build, configure_script, options):
    build.mkdir(exist_ok=True)
    if not (build / 'Makefile').exists():
        subprocess.run([str(configure_script), *common, *options],
                       cwd=build, env=env, check=True)


binutils = work / 'binutils-build'
configure(binutils, source / 'binutils-2.47/configure',
          ['--disable-gdb', '--disable-sim', '--disable-gprofng',
           '--disable-werror', '--enable-static'])
subprocess.run(['make', f'-j{args.jobs}', 'all-binutils', 'all-gas', 'all-ld'],
               cwd=binutils, env=env, check=True)
for directory, binary, trigger in (('gas', 'as-new', 'as.o'),
                                   ('ld', 'ld-new', 'ldmain.o')):
    (binutils / directory / trigger).touch()
    subprocess.run(['make', '-C', str(binutils / directory),
                    'LDFLAGS=-all-static -Wl,-Ttext-segment=0x80000000', binary],
                   env=env, check=True)

gcc = work / 'gcc-build'
configure(gcc, gcc_source / 'configure',
          ['--libexecdir=/usr/libexec', '--enable-languages=c,c++',
           '--disable-bootstrap', '--disable-multilib',
           '--disable-libsanitizer', '--disable-libquadmath',
           '--disable-libssp', '--disable-libgomp', '--disable-libatomic',
           '--disable-libstdcxx-pch', '--without-isl'])
subprocess.run(['make', f'-j{args.jobs}', 'all-gcc'], cwd=gcc, env=env, check=True)
from install_gcc_toolchain import validate_elf
for binary in (gcc / 'gcc/xgcc', gcc / 'gcc/xg++', gcc / 'gcc/cc1',
               gcc / 'gcc/cc1plus', gcc / 'gcc/collect2',
               binutils / 'gas/as-new', binutils / 'ld/ld-new'):
    validate_elf(binary)
print(f'GCC_BUILD={gcc / "gcc"}')
print(f'BINUTILS_BUILD={binutils}')
