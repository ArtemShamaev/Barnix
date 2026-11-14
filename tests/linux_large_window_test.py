"""Run an ordinary Linux i386 probe linked into the toolchain window."""
import pathlib
import shutil
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-large-window-') as temporary:
    directory = pathlib.Path(temporary)
    image = directory / 'live.img'
    shutil.copyfile('live-ext2.img', image)
    probe = directory / 'linux-high-probe.elf'
    shutil.copyfile('userland/linux-high-probe.elf', probe)
    subprocess.run(['python3', 'scripts/install_apps.py', str(image), str(probe)], check=True)
    debug = directory / 'debug.log'
    result = subprocess.run([
        'qemu-system-i386', '-m', '2048M', '-kernel', 'kernel-test.elf',
        '-initrd', str(image), '-append', 'linux-selftest', '-display', 'none',
        '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-debugcon', f'file:{debug}', '-global', 'isa-debugcon.iobase=0xe9',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=60)
    assert result.returncode == 1, (result.returncode, result.stderr, debug.read_text(errors='replace')[-1000:])
print('Linux i386 high window passed in QEMU')
