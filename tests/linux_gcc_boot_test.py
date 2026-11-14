"""Boot a stripped static GCC driver inside Barnix and check its exit status."""
import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-gcc-boot-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    debug = directory / 'debug.log'
    gcc = directory / 'gcc'
    source = pathlib.Path('/tmp/barnix-gcc-port/build2/gcc/xgcc')
    if not source.exists():
        raise SystemExit('Build the i386 GCC driver before running this test')
    gcc.write_bytes(source.read_bytes())
    subprocess.run(['strip', '--strip-all', str(gcc)], check=True)
    with image.open('wb') as stream:
        stream.truncate(64 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '512', '-O', 'none,filetype', str(image)], check=True)
    subprocess.run(['python3', 'scripts/install_apps.py', str(image), str(gcc)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
        '-initrd', str(image), '-append', 'linux-selftest linux-gcc',
        '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=30)
    print(debug.read_text(errors='replace'))
    assert result.returncode == 1, (result.returncode, result.stderr)
print('GCC driver ran inside Barnix')
