"""CPU exceptions show a KERNEL PANIC with their cause instead of rebooting.

Each case boots the live ISO with tests/fault_probe.elf on a scratch disk,
runs one deliberate fault and reads the panic from the VGA text screen.
"""
import pathlib
import shutil
import subprocess
import tempfile

from qemu_shell import run

ROOT = pathlib.Path(__file__).resolve().parents[1]
CASES = [
    ('div', '#DE divide error', 'In a program'),
    ('gp', '#GP general protection', 'In a program'),
    ('stack', 'Likely cause', "program's stack overflowed"),
    ('core', '#DE divide error', 'extra processor core'),
]

with tempfile.TemporaryDirectory(prefix='barnix-fault-') as temporary:
    directory = pathlib.Path(temporary)
    for argument, cause, where in CASES:
        disk = directory / 'disk.img'
        shutil.copyfile(ROOT / 'live-ext2.img', disk)
        subprocess.run(['debugfs', '-w', '-R', f'write {ROOT / "tests/fault_probe.elf"} /faultprobe', str(disk)],
                       check=True, capture_output=True)
        subprocess.run(['debugfs', '-w', '-R', 'set_inode_field /faultprobe mode 0100755', str(disk)],
                       check=True, capture_output=True)
        # The panic halts the machine: the "prompt" to wait for is the panic text.
        screen = run(disk, directory, [(f'/faultprobe {argument}', 'KERNEL PANIC')],
                     extra_args=['-smp', '4'], command_timeout=20, halts=True)
        assert cause in screen and where in screen, (argument, screen)
        print(f'{argument}: panic shows "{cause}" ({where})')
print('CPU exception reports passed')
