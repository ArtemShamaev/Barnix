"""Read and execute an ELF larger than the former ext2 16 MiB file limit."""
import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-big-file-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    probe = directory / 'linux-20m-probe.elf'
    with image.open('wb') as stream:
        stream.truncate(128 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '512', '-O', 'none,filetype', str(image)], check=True)
    data = pathlib.Path('userland/linux-probe.elf').read_bytes()
    probe.write_bytes(data + bytes(20 * 1024 * 1024 - len(data)))
    subprocess.run(['python3', 'scripts/install_apps.py', str(image), str(probe)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
        '-initrd', str(image), '-append', 'linux-selftest linux-bigfile',
        '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=90)
    assert result.returncode == 1, (result.returncode, result.stderr)
print('20 MiB Linux ELF passed ext2, execve and normal launcher checks')
