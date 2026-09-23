"""Run the process probes through the normal ISO and its ATA filesystem."""
import pathlib
import shutil
import subprocess
import tempfile
from qemu_shell import run

with tempfile.TemporaryDirectory(prefix='barnix-process-shell-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'process.img'
    shutil.copyfile('live-ext2.img', disk)
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk),
                    'userland/exec-parent.elf', 'userland/exec-child.elf',
                    'userland/fork-probe.elf'], check=True)
    run(disk, directory, [
        ('./fork-probe.elf /bin/true', 'program exited: 43'),
        ('pwd', 'bssh> pwd\n/'),
        ('./exec-parent.elf /bin/exec-child.elf', 'program exited: 42'),
        ('hello.elf after', 'program exited: 7'),
        ('echo recovered', 'recovered'),
    ], command_timeout=45)
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    for name, expected in [('fork-state.dat', b'AB'), ('exec-state.dat', b'A' + b'B' * 33)]:
        result = subprocess.run(['debugfs', '-R', f'cat /{name}', str(disk)], capture_output=True, check=True)
        assert result.stdout == expected, (name, result.stdout)
print('Normal ISO: fork/exec/wait, fault recovery, legacy return and ATA persistence passed')
