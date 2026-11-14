"""Exercise the disk-backed Linux ELF staging path above the old 640 KiB cap."""
import pathlib
import shutil
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-large-elf-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    probe = directory / 'linux-large-probe.elf'
    shutil.copyfile('live-ext2.img', image)
    data = pathlib.Path('userland/linux-probe.elf').read_bytes()
    assert len(data) < 640 * 1024
    probe.write_bytes(data + bytes(1024 * 1024 - len(data)))
    subprocess.run(['python3', 'scripts/install_apps.py', str(image), str(probe)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '256M', '-kernel', 'kernel-test.elf',
        '-initrd', str(image), '-append', 'linux-selftest', '-display', 'none',
        '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=30)
    assert result.returncode == 1, (result.returncode, result.stderr)
print('Linux ELF above 640 KiB loaded through execve and the normal launcher')
