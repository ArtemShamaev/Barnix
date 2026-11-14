"""Compile and execute a C program using GCC inside the Barnix guest."""
import pathlib
import shutil
import subprocess
import tempfile

gcc_build = pathlib.Path('/tmp/barnix-gcc-port/build2/gcc')
binutils_build = pathlib.Path('/tmp/barnix-binutils-port/build')
binaries = {
    '/usr/bin/gcc': gcc_build / 'xgcc',
    '/usr/bin/g++': gcc_build / 'xg++',
    '/usr/libexec/gcc/i686-pc-linux-gnu/16.2.0/cc1': gcc_build / 'cc1',
    '/usr/libexec/gcc/i686-pc-linux-gnu/16.2.0/cc1plus': gcc_build / 'cc1plus',
    '/usr/libexec/gcc/i686-pc-linux-gnu/16.2.0/collect2': gcc_build / 'collect2',
    '/usr/bin/as': binutils_build / 'gas/as-new',
    '/usr/bin/ld': binutils_build / 'ld/ld-new',
}
for source in binaries.values():
    if not source.exists():
        raise SystemExit(f'Missing toolchain binary: {source}')

with tempfile.TemporaryDirectory(prefix='barnix-gcc-compile-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    debug = directory / 'debug.log'
    with image.open('wb') as stream:
        stream.truncate(256 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '4096', '-O', 'none,filetype', str(image)], check=True)

    def disk(command):
        result = subprocess.run(['debugfs', '-w', '-R', command, str(image)],
                                capture_output=True, text=True, check=True)
        return result.stdout

    directories = {'/'}
    def mkdir(path):
        parent = str(pathlib.PurePosixPath(path).parent)
        if parent not in directories:
            mkdir(parent)
        if path not in directories:
            disk(f'mkdir {path}')
            directories.add(path)

    def add(source, destination, executable=False):
        mkdir(str(pathlib.PurePosixPath(destination).parent))
        if 'Allocated inode:' not in disk(f'write "{source}" {destination}'):
            raise RuntimeError(f'Could not import {destination}')
        disk(f'set_inode_field {destination} mode {"0100755" if executable else "0100644"}')

    for destination, source in binaries.items():
        copied = directory / (destination.replace('/', '_') + '.elf')
        copied.write_bytes(source.read_bytes())
        subprocess.run(['strip', '--strip-all', str(copied)], check=True)
        add(copied, destination, True)
    mkdir('/tmp')
    mkdir('/bin')
    mkdir('/usr/include')
    predef = directory / 'stdc-predef.h'
    predef.write_text('/* Barnix toolchain predefines are provided by GCC. */\n')
    add(predef, '/usr/include/stdc-predef.h')
    hello = directory / 'hello.c'
    hello.write_text('''void _start(void) {
        const char message[] = "C compiler passed\\n";
        __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(message), "d"(18) : "memory");
        __asm__ volatile("int $0x80" :: "a"(1), "b"(42) : "memory");
        for (;;) {}
    }
    ''')
    add(hello, '/hello.c')
    cpp = directory / 'hello.cpp'
    cpp.write_text('''template<int N> struct Sum { static constexpr int value = N + Sum<N - 1>::value; };
    template<> struct Sum<0> { static constexpr int value = 0; };
    static_assert(Sum<6>::value == 21);
    extern "C" void _start() {
        const char message[] = "C++ compiler passed\\n";
        __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(message), "d"(20) : "memory");
        __asm__ volatile("int $0x80" :: "a"(1), "b"(43) : "memory");
        for (;;) {}
    }
    ''')
    add(cpp, '/hello.cpp')
    subprocess.run(['e2fsck', '-fn', str(image)], capture_output=True, check=True)
    try:
        result = subprocess.run([
            'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
            '-initrd', str(image), '-append', 'linux-selftest linux-compile',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
        ], capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        print('\n'.join(debug.read_text(errors='replace').splitlines()[-40:]))
        raise
    shutil.copyfile(debug, '/tmp/barnix-gcc-last.log')
    lines = debug.read_text(errors='replace').splitlines()
    print('\n'.join(line for line in lines[-400:] if not (len(line) == 5 and line.startswith('S')))[-5000:])
    assert result.returncode == 1, (result.returncode, result.stderr)
print('GCC compiled and ran C inside Barnix')
