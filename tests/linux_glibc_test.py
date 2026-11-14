"""Boot static 32-bit glibc and libstdc++ programs in Barnix."""
import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-glibc-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    debug = directory / 'debug.log'
    c = directory / 'hello-libc.c'
    cpp = directory / 'hello-cpp-libc.cpp'
    c.write_text('#include <stdio.h>\nint main(void) { puts("glibc C passed"); return 41; }\n')
    cpp.write_text('''#include <iostream>
    #include <vector>
    int main() {
        std::vector<int> values{20, 23};
        std::cout << "libstdc++ C++ passed " << values[0] + values[1] << '\\n';
        return values[0] + values[1];
    }
    ''')
    c_binary = directory / 'hello-libc.elf'
    cpp_binary = directory / 'hello-cpp-libc.elf'
    flags = ['-m32', '-static', '-no-pie', '-Wl,-Ttext-segment=0x80000000']
    subprocess.run(['gcc', *flags, str(c), '-o', str(c_binary)], check=True)
    subprocess.run(['g++', *flags, str(cpp), '-o', str(cpp_binary)], check=True)
    subprocess.run(['strip', '--strip-all', str(c_binary), str(cpp_binary)], check=True)
    with image.open('wb') as stream:
        stream.truncate(64 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '512', '-O', 'none,filetype', str(image)], check=True)
    subprocess.run(['python3', 'scripts/install_apps.py', str(image), str(c_binary),
                    str(cpp_binary)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
        '-initrd', str(image), '-append', 'linux-selftest linux-libc',
        '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=120)
    lines = debug.read_text(errors='replace').splitlines()
    print('\n'.join(line for line in lines[-250:] if not (len(line) == 5 and line.startswith('S')))[-4000:])
    assert result.returncode == 1, (result.returncode, result.stderr)
print('Static glibc C and libstdc++ C++ ran in Barnix')
