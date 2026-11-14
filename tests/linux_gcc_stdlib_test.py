"""Compile and run C/glibc and C++/libstdc++ programs in the Barnix guest."""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from scripts.install_gcc_toolchain import install


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gcc-build', type=pathlib.Path,
                    default=pathlib.Path('/tmp/barnix-gcc-port/build2/gcc'))
parser.add_argument('--binutils-build', type=pathlib.Path,
                    default=pathlib.Path('/tmp/barnix-binutils-port/build'))
parser.add_argument('--cpp-only', action='store_true')
parser.add_argument('--timeout', type=int, default=1200)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='barnix-gcc-stdlib-') as directory:
    directory = pathlib.Path(directory)
    image = directory / 'toolchain.img'
    debug = directory / 'debug.log'
    with image.open('wb') as stream:
        stream.truncate(384 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '8192', '-O', 'none,filetype', str(image)], check=True)
    print(f'Installed {install(image, args.gcc_build, args.binutils_build)} files', flush=True)
    sources = {
        '/hello-stdlib.c': '#include <stdio.h>\n'
                           'int main(void) { puts("guest glibc C passed"); return 41; }\n',
        '/hello-cpp-stdlib.cpp': '#include <iostream>\n#include <vector>\n'
                                 'int main() { std::vector<int> v{20, 23}; '
                                 'std::cout << "guest C++ passed " << v[0] + v[1] << "\\n"; '
                                 'return v[0] + v[1]; }\n',
    }
    if args.cpp_only:
        del sources['/hello-stdlib.c']
    commands = []
    for path, contents in sources.items():
        source = directory / path[1:]
        source.write_text(contents)
        commands.append(f'write "{source}" {path}')
    commands.append('mkdir /bin')
    script = directory / 'debugfs.commands'
    script.write_text('\n'.join(commands) + '\n')
    subprocess.run(['debugfs', '-w', '-f', str(script), str(image)],
                   check=True, capture_output=True)
    try:
        result = subprocess.run([
            'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
            '-initrd', str(image), '-append', 'linux-selftest linux-stdlib-compile',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
        ], capture_output=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        shutil.copyfile(debug, '/tmp/barnix-gcc-stdlib-timeout.log')
        print('\n'.join(debug.read_text(errors='replace').splitlines()[-50:]), flush=True)
        raise
    lines = debug.read_text(errors='replace').splitlines()
    print('\n'.join(lines[-200:])[-8000:])
    assert result.returncode == 1, (result.returncode, result.stderr)
print('GCC compiled and ran ' +
      ('C++/libstdc++' if args.cpp_only else 'C/glibc and C++/libstdc++') +
      ' inside Barnix')
