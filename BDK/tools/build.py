#!/usr/bin/env python3
"""Build a native Barnix ELF using only the SDK next to this script."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def build(project, native_sources=None):
    sdk = Path(__file__).resolve().parent.parent
    project = Path(project).resolve()
    sources = native_sources if native_sources is not None else sorted(p for p in (project / 'src').rglob('*') if p.suffix in ('.c', '.cpp', '.cc', '.cxx'))
    csharp = sorted((project / 'src').rglob('*.cs'))
    if csharp and native_sources is None:
        if sources:
            raise ValueError('Mixing C# and C/C++ sources is not supported')
        with tempfile.TemporaryDirectory(prefix='bdk-csharp-') as temporary:
            generated = Path(temporary) / 'main.cpp'
            run([os.environ.get('DOTNET_HOST_PATH', 'dotnet'), 'run', '--project', sdk / 'tools/csharp/Compiler.csproj', '--',
                 project / 'src', generated])
            return build(project, [generated])
    entries = [p for p in sources if p.stem == 'main']
    if len(entries) != 1:
        raise ValueError('src must contain exactly one main.c or main.cpp entry file')
    cc = os.environ.get('CC', 'clang' if os.name == 'nt' else 'gcc')
    cxx = os.environ.get('CXX', 'clang++' if os.name == 'nt' else 'g++')
    clang = 'clang' in run([cc, '--version'], capture_output=True, text=True).stdout.lower()
    flags = ['-m32', '-march=i386', '-mno-sse', '-mno-mmx', '-msoft-float', '-O2',
             '-fwrapv', '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-pic', '-fno-stack-protector',
             '-Wall', '-Wextra', '-nostdinc', '-DBARNIX_BDK', '-I', sdk / 'include', '-I', sdk / 'runtime',
             '-I', project / 'src']
    # Only compiler-internal freestanding headers (stddef/stdarg/etc), never host libc.
    if clang:
        flags += ['--target=i386-unknown-none-elf']
        internal = str(Path(run([cc, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()) / 'include')
    else:
        internal = run([cc, '-print-file-name=include'], capture_output=True, text=True).stdout.strip()
    flags += ['-isystem', internal]

    def compile_flags(path):
        if path.suffix == '.c':
            return [cc, '-std=c11', *flags]
        return [cxx, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                '-fno-use-cxa-atexit', *flags,
                # Clang freestanding C++ mangles main; retain its standard entry
                # semantics without enabling host includes, libraries or builtins.
                *(['-fhosted', '-fno-builtin'] if clang else [])]

    macros = run([*compile_flags(entries[0]), '-dM', '-E', entries[0]], capture_output=True, text=True).stdout
    match = re.search(r'^#define BARNIX_APP_NAME "([A-Za-z0-9_][A-Za-z0-9_.-]*\.elf)"$', macros, re.M)
    if not match:
        raise ValueError('Define BARNIX_APP_NAME "app.elf" in src/main.c or src/main.cpp (a filename, not a path)')
    if len(match[1]) > 23:
        raise ValueError('Barnix filenames are limited to 23 characters, including .elf')
    output = project / 'elf' / match[1]
    output.parent.mkdir(exist_ok=True)
    # Every build gets fresh objects; only replace the last successful ELF after linking.
    with tempfile.TemporaryDirectory(prefix='bdk-build-') as temporary:
        temporary = Path(temporary)
        objects = []
        for index, source in enumerate([*sorted((sdk / 'runtime').glob('*.c')), *sources]):
            obj = temporary / f'{index}.o'
            run([*compile_flags(source), '-c', source, '-o', obj])
            objects.append(obj)
        linked = temporary / 'app.elf'
        if clang:
            linker = os.environ.get('BDK_LD') or shutil.which('ld.lld') or shutil.which('lld')
            if not linker:
                raise ValueError('LLVM lld linker is missing; rerun the BDK installer')
            flavor = ['-flavor', 'gnu'] if Path(linker).stem.lower() == 'lld' else []
            run([linker, *flavor, '-m', 'elf_i386', '--build-id=none',
                 '-T', sdk / 'runtime' / 'app.ld', *objects, '-o', linked])
        else:
            run([cc, '-m32', '-nostdlib', '-static', '-no-pie', '-Wl,--build-id=none',
                 '-Wl,-T,' + str(sdk / 'runtime' / 'app.ld'), *objects, '-o', linked])
        data = bytearray(linked.read_bytes())
        if data[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('compiler did not produce little-endian ELF32')
        header = (sdk / 'include' / 'barnix' / 'app_abi.h').read_text()
        version_match = re.search(r'#define BARNIX_APP_ABI (\d+)U', header)
        if not version_match:
            # In a source checkout the public SDK header is a thin wrapper;
            # packaged SDKs contain the expanded header directly.
            version_match = re.search(r'#define BARNIX_APP_ABI (\d+)U',
                                      (sdk.parent / 'app_abi.h').read_text())
        version = int(version_match[1])
        data[7:9] = bytes([255, version])
        # Use the destination filesystem for an atomic replacement.
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.bdk-', delete=False) as pending:
            pending.write(data)
        try:
            os.replace(pending.name, output)
        finally:
            Path(pending.name).unlink(missing_ok=True)
    print(f'BDK: {output}', flush=True)
    return output


if __name__ == '__main__':
    try:
        build(sys.argv[1] if len(sys.argv) > 1 else Path.cwd())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        print(f'BDK build failed: {error}', file=sys.stderr)
        sys.exit(1)
