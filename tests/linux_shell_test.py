"""Check Linux program launches and legacy commands in the same boot."""
import pathlib
import shutil
import subprocess
import tempfile
from qemu_shell import run

with tempfile.TemporaryDirectory(prefix='barnix-linux-shell-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'test.img'
    shutil.copyfile('live-ext2.img', disk)
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk),
                    'userland/linux-probe.elf'], check=True)
    run(disk, directory, [
        ('hello.elf before', 'program exited: 7'),
        ('./linux-probe.elf argument', 'program exited: 37'),
        ('pwd', 'bssh> pwd\n/'),
        ('echo linux commands', 'linux commands'),
        ('write next hello', 'program exited: 0'),
        ('append next world', 'program exited: 0'),
        ('cp next copied', 'program exited: 0'),
        ('cat copied', 'helloworld'),
        ('ls', 'copied'),
        ('true', 'program exited: 0'),
        ('false', 'program exited: 1'),
        ('/bin/linux-probe.elf argument', 'program exited: 37'),
        ('cat linux-probe.dat', 'redirected'),
        ('./linux-probe.elf fault', 'program exited: 139'),
        ('linux /bin/linux-probe.elf privileged', 'Linux program exited: 139'),
        ('linux /bin/linux-probe.elf argument', 'Linux program exited: 37'),
        ('hello.elf after', 'program exited: 7'),
    ])
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
print('Linux/legacy shell integration and fault recovery passed')
