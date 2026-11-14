#!/usr/bin/env python3
"""Build the pinned musl support archive for the freestanding JS engine.
Only referenced objects are linked. OS calls fail with ENOSYS; no Linux ABI
or TLS register is assumed by native Barnix applications.
"""
import pathlib
import subprocess
import tarfile

root = pathlib.Path(__file__).resolve().parents[1]
out = root / 'apps/gouse/obj/musl'
out.mkdir(parents=True, exist_ok=True)
with tarfile.open(root / 'apps/gouse/vendor/musl-1.2.5.tar.gz') as archive:
    for member in archive.getmembers():
        member.name = '/'.join(member.name.split('/')[1:])
        if member.name:
            archive.extract(member, out, filter='data')
arch = out / 'arch/i386'
p = arch / 'pthread_arch.h'
p.write_text('extern uintptr_t bw_thread_pointer(void);\n'
             'static inline uintptr_t __get_tp(void) { return bw_thread_pointer(); }\n'
             '#define MC_PC gregs[REG_EIP]\n')
p = arch / 'syscall_arch.h'
s = '#define __SYSCALL_LL_E(x) (long)(x), (long)((unsigned long long)(x)>>32)\n#define __SYSCALL_LL_O(x) __SYSCALL_LL_E(x)\n'
for n in range(7):
    args = ', '.join(['long n'] + [f'long a{i}' for i in range(n)])
    s += f'static inline long __syscall{n}({args}) {{ return -38; }}\n'
s += '#define VDSO_USEFUL 0\n'
p.write_text(s)
subprocess.run(['./configure', '--target=i386', '--disable-shared',
                'CC=gcc -m32', 'AR=ar', 'RANLIB=ranlib', 'CFLAGS=-Os -march=pentium4 -mfpmath=sse -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections'], cwd=out, check=True)
subprocess.run(['make', '-j4', 'lib/libc.a'], cwd=out, check=True)
