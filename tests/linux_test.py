"""Run one unmodified Linux i386 ELF on Linux and in Barnix ring 3, with fault recovery."""
import pathlib
import subprocess
import tempfile

probe = pathlib.Path('userland/linux-probe.elf').resolve()
with tempfile.TemporaryDirectory(prefix='barnix-linux-') as directory:
    host = subprocess.run([str(probe), 'argument'], env={}, cwd=directory, capture_output=True)
    assert host.returncode == 37, (host.returncode, host.stdout, host.stderr)
    assert host.stdout == b'Linux ABI probe passed\n', host.stdout
    contents = (pathlib.Path(directory) / 'linux-probe.dat').read_bytes()
    assert contents == b'redirected', contents
    log = pathlib.Path(directory) / 'qemu.log'
    result = subprocess.run([
        'qemu-system-i386', '-m', '64M', '-kernel', 'kernel-test.elf',
        '-initrd', 'live-ext2.img', '-append', 'linux-selftest', '-display', 'none', '-serial', 'none',
        '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
        '-d', 'guest_errors', '-D', str(log),
    ], capture_output=True, timeout=20)
    assert result.returncode == 1, (result.returncode, result.stderr, log.read_text())
print('Linux host and Barnix ring-3 ABI tests passed (including repeat execution)')
