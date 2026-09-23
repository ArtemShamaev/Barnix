"""Compare pipe behavior on Linux and Barnix, then verify the normal ISO path."""
import pathlib
import resource
import shutil
import subprocess
import tempfile
from qemu_shell import run


def limit_descriptors():
    resource.setrlimit(resource.RLIMIT_NOFILE, (16, 16))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


probe = pathlib.Path('userland/pipe-probe.elf').resolve()
echo = pathlib.Path('apps/bin/echo').resolve()
cat = pathlib.Path('apps/bin/cat').resolve()
with tempfile.TemporaryDirectory(prefix='barnix-pipe-') as temporary:
    directory = pathlib.Path(temporary)
    host = subprocess.run([str(probe), str(echo), str(cat)], env={}, cwd=directory,
                          capture_output=True, timeout=30, preexec_fn=limit_descriptors)
    assert host.returncode == 44, (host.returncode, host.stdout, host.stderr)
    assert host.stdout == b'pipe blocking/nonblocking/fork/exec passed\n', host.stdout
    assert (directory / 'pipe-state.dat').read_bytes() == b'pipe exec passed\n'
    disk = directory / 'pipe.img'
    shutil.copyfile('live-ext2.img', disk)
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk), str(probe)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '64M', '-kernel', 'kernel-test.elf',
        '-initrd', str(disk), '-append', 'linux-selftest', '-display', 'none',
        '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=60)
    assert result.returncode == 1, (result.returncode, result.stderr)
    run(disk, directory, [
        ('./pipe-probe.elf /bin/echo /bin/cat', 'program exited: 44'),
        ('cat pipe-state.dat', 'pipe exec passed'),
        ('./pipe-probe.elf deadlock', 'program exited: 125'),
        ('./pipe-probe.elf /bin/echo /bin/cat', 'program exited: 44'),
        ('hello.elf after', 'program exited: 7'),
    ], command_timeout=45)
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    contents = subprocess.run(['debugfs', '-R', 'cat /pipe-state.dat', str(disk)],
                              capture_output=True, check=True)
    assert contents.stdout == b'pipe exec passed\n', contents.stdout
print('Pipes passed on Linux and Barnix: blocking, vectors, EOF, SIGPIPE, fork/exec and ISO recovery')
