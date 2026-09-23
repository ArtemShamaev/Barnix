"""Exec replacement, arguments/environment, descriptor inheritance and failures."""
import pathlib
import resource
import shutil
import subprocess
import tempfile

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

parent = pathlib.Path('userland/exec-parent.elf').resolve()
fork_probe = pathlib.Path('userland/fork-probe.elf').resolve()
true = pathlib.Path('apps/bin/true').resolve()
child = pathlib.Path('userland/exec-child.elf').resolve()
with tempfile.TemporaryDirectory(prefix='barnix-execve-') as temporary:
    directory = pathlib.Path(temporary)
    result = subprocess.run([str(parent), str(child)], env={}, cwd=directory, capture_output=True)
    assert result.returncode == 42, (result.returncode, result.stdout, result.stderr)
    assert result.stdout == b'execve argv/env/fds/replacement passed\n', result.stdout
    assert (directory / 'exec-state.dat').read_bytes() == b'A' + b'B' * 33
    result = subprocess.run([str(fork_probe), str(true)], env={}, cwd=directory, capture_output=True)
    assert result.returncode == 43, (result.returncode, result.stdout, result.stderr)
    assert result.stdout == b'fork/wait/exec isolation passed\n', result.stdout
    assert (directory / 'fork-state.dat').read_bytes() == b'AB'
    disk = directory / 'exec.img'
    shutil.copyfile('live-ext2.img', disk)
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk), str(parent), str(child), str(fork_probe)], check=True)
    result = subprocess.run([
        'qemu-system-i386', '-m', '64M', '-kernel', 'kernel-test.elf',
        '-initrd', str(disk), '-append', 'linux-selftest', '-display', 'none',
        '-serial', 'none', '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    ], capture_output=True, timeout=45)
    assert result.returncode == 1, (result.returncode, result.stderr)
print('execve passed on Linux and Barnix: 34 image replacements, argv/env, cwd, CLOEXEC and errors')

print("fork/waitpid passed on Linux and Barnix: isolated memory/cwd, shared offsets, exec, faults and slot reuse")
