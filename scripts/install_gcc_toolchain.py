"""Install the locally built static i386 GCC toolchain into an ext2 image.

The host supplies 32-bit glibc and GCC 16 development headers and archives.
The compiler and binutils executables must already be built as static i386
ELF files based at 0x80000000 for Barnix's large process window.
"""
import argparse
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile

TARGET = 'i686-pc-linux-gnu'
VERSION = '16.2.0'
GCC_LIB = f'/usr/lib/gcc/{TARGET}/{VERSION}'
GCC_EXEC = f'/usr/libexec/gcc/{TARGET}/{VERSION}'
CPP_INCLUDE = f'/usr/include/c++/{VERSION}'


def toolchain_files(gcc_build, binutils_build, host_root=pathlib.Path('/')):
    gcc_build = pathlib.Path(gcc_build)
    binutils_build = pathlib.Path(binutils_build)
    host_root = pathlib.Path(host_root)
    files = {
        '/usr/bin/gcc': (gcc_build / 'xgcc', True),
        '/usr/bin/g++': (gcc_build / 'xg++', True),
        '/usr/bin/as': (binutils_build / 'gas/as-new', True),
        '/usr/bin/ld': (binutils_build / 'ld/ld-new', True),
        f'{GCC_EXEC}/cc1': (gcc_build / 'cc1', True),
        f'{GCC_EXEC}/cc1plus': (gcc_build / 'cc1plus', True),
        f'{GCC_EXEC}/collect2': (gcc_build / 'collect2', True),
    }
    for include_dir in ('include', 'include-fixed'):
        source_dir = gcc_build / include_dir
        if source_dir.exists():
            for source in source_dir.rglob('*'):
                if source.is_file():
                    files[f'{GCC_LIB}/{include_dir}/{source.relative_to(source_dir)}'] = (source, False)

    host_lib32 = host_root / 'usr/lib32'
    for name in ('crt1.o', 'Scrt1.o', 'crti.o', 'crtn.o', 'libc.a',
                 'libc_nonshared.a', 'libm.a', 'libdl.a', 'libpthread.a', 'libresolv.a'):
        source = host_lib32 / name
        if source.exists():
            files[f'/usr/lib/{name}'] = (source, False)
    host_gcc32 = host_root / 'usr/lib/gcc/x86_64-pc-linux-gnu/16/32'
    for name in ('crtbegin.o', 'crtbeginS.o', 'crtbeginT.o', 'crtend.o',
                 'crtendS.o', 'libgcc.a', 'libgcc_eh.a', 'libstdc++.a',
                 'libsupc++.a', 'libstdc++exp.a', 'libstdc++fs.a'):
        source = host_gcc32 / name
        if source.exists():
            files[f'{GCC_LIB}/{name}'] = (source, False)

    host_include = host_root / 'usr/include'
    for source in host_include.iterdir():
        if source.is_file() and source.suffix == '.h':
            files[f'/usr/include/{source.name}'] = (source, False)
    for directory in ('bits', 'gnu', 'sys', 'asm', 'asm-generic', 'linux',
                      'netinet', 'arpa', 'net', 'netpacket', 'endian.h'):
        source_dir = host_include / directory
        if source_dir.is_dir():
            for source in source_dir.rglob('*'):
                if source.is_file():
                    files[f'/usr/include/{source.relative_to(host_include)}'] = (source, False)

    host_cpp = host_include / 'c++/16'
    if host_cpp.is_dir():
        for source in host_cpp.rglob('*'):
            if source.is_file() and 'x86_64-pc-linux-gnu' not in source.parts:
                files[f'{CPP_INCLUDE}/{source.relative_to(host_cpp)}'] = (source, False)
        target_config = host_cpp / 'x86_64-pc-linux-gnu/32'
        for source in target_config.rglob('*'):
            if source.is_file():
                files[f'{CPP_INCLUDE}/{TARGET}/{source.relative_to(target_config)}'] = (source, False)
    required = ('/usr/lib/crt1.o', '/usr/lib/libc.a', f'{GCC_LIB}/libgcc.a',
                f'{GCC_LIB}/libstdc++.a', f'{CPP_INCLUDE}/{TARGET}/bits/c++config.h')
    for destination in required:
        if destination not in files:
            raise FileNotFoundError(f'Missing 32-bit development dependency: {destination}')
    return files


def validate_elf(source):
    with source.open('rb') as stream:
        header = stream.read(52)
        if len(header) != 52 or header[:5] != b'\x7fELF\x01':
            raise ValueError(f'Not an ELF32 executable: {source}')
        etype, machine = struct.unpack_from('<HH', header, 16)
        if etype != 2 or machine != 3:
            raise ValueError(f'Expected static i386 ET_EXEC: {source}')
        offset = struct.unpack_from('<I', header, 28)[0]
        count = struct.unpack_from('<H', header, 44)[0]
        stream.seek(offset)
        programs = [stream.read(32) for _ in range(count)]
    loads = [struct.unpack_from('<I', item, 8)[0] for item in programs
             if len(item) == 32 and struct.unpack_from('<I', item)[0] == 1]
    if not loads or min(loads) != 0x80000000 or any(
            struct.unpack_from('<I', item)[0] == 3 for item in programs if len(item) == 32):
        raise ValueError(f'Expected static i386 ELF based at 0x80000000: {source}')


def install(image, gcc_build, binutils_build, host_root=pathlib.Path('/')):
    image = pathlib.Path(image)
    files = toolchain_files(gcc_build, binutils_build, host_root)
    with tempfile.TemporaryDirectory(prefix='barnix-gcc-files-', dir=image.parent) as temporary:
        temporary = pathlib.Path(temporary)
        # A GCC configured without shared libgcc otherwise omits the unwind
        # archive when linking static glibc/libstdc++ programs.
        specs = temporary / 'specs'
        # Barnix has no dynamic loader: link statically unless told otherwise.
        specs.write_text('*link_emulation:\nelf_i386\n\n'
                         '*libgcc:\n-lgcc -lgcc_eh\n\n'
                         '*self_spec:\n+ %{!shared:%{!static:-static}}\n\n')
        files[f'{GCC_LIB}/specs'] = (specs, False)
        for destination, (source, executable) in list(files.items()):
            if not source.exists():
                raise FileNotFoundError(source)
            if executable:
                validate_elf(source)
                stripped = temporary / destination.replace('/', '_')
                shutil.copyfile(source, stripped)
                subprocess.run(['strip', '--strip-all', str(stripped)], check=True)
                if stripped.stat().st_size >= 64 * 1024 * 1024:
                    raise ValueError(f'Executable exceeds Barnix file limit: {source}')
                files[destination] = (stripped, True)
        commands = []
        directories = {'/'}

        def ensure_directory(path):
            if path in directories:
                return
            parent = str(pathlib.PurePosixPath(path).parent)
            ensure_directory(parent)
            commands.append(f'mkdir {path}')
            directories.add(path)

        ensure_directory('/tmp')
        commands.append('set_inode_field /tmp mode 041777')
        for destination, (source, executable) in sorted(files.items()):
            if any(len(part) > 255 for part in pathlib.PurePosixPath(destination).parts):
                raise ValueError(f'Ext2 name too long: {destination}')
            ensure_directory(str(pathlib.PurePosixPath(destination).parent))
            if not re.fullmatch(r'[\w./+\-]+', destination, re.ASCII):
                raise ValueError(f'Unsafe ext2 destination: {destination}')
            if any(character in str(source) for character in '"\n\r\\'):
                raise ValueError(f'Unsafe source pathname: {source}')
            commands.append(f'write "{source}" {destination}')
            if executable:
                commands.append(f'set_inode_field {destination} mode 0100755')
        script = temporary / 'debugfs.commands'
        script.write_text('\n'.join(commands) + '\n')
        result = subprocess.run(['debugfs', '-w', '-f', str(script), str(image)],
                                capture_output=True, text=True, check=True)
        written = result.stdout.count('Allocated inode:')
        if written != len(files):
            raise RuntimeError(f'Imported {written}/{len(files)} toolchain files:\n'
                               + result.stderr[-3000:])
        subprocess.run(['e2fsck', '-fn', str(image)], capture_output=True, check=True)
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=pathlib.Path)
    parser.add_argument('--gcc-build', required=True, type=pathlib.Path)
    parser.add_argument('--binutils-build', required=True, type=pathlib.Path)
    parser.add_argument('--host-root', default=pathlib.Path('/'), type=pathlib.Path)
    args = parser.parse_args()
    count = install(args.image, args.gcc_build, args.binutils_build, args.host_root)
    print(f'Installed {count} GCC toolchain files into {args.image}')


if __name__ == '__main__':
    main()
