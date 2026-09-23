"""Smoke-test the built ISO using a temporary disk and QEMU's VGA memory."""
import pathlib
import shutil
import subprocess
import tempfile
import time


from qemu_shell import run


# Program counts change as userland migrates; account for the actual template.
base_free_inodes = int.from_bytes(pathlib.Path("live-ext2.img").read_bytes()[1040:1044], "little")

with tempfile.TemporaryDirectory(prefix='barnix-shell-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'test.img'
    shutil.copyfile('live-ext2.img', disk)
    run(disk, directory, [
        ('help', 'run a Barnix ELF32 program'),
        ('/bin/echo absolute', 'absolute'),
        ('./bin/echo relative', 'relative'),
        ('hello.elf argcheck', 'program exited: 7'),
        ('hello.elf again', 'program exited: 7'),
        ('write a hello', 'bssh>'),
        ('append a world', 'bssh>'),
        ('cp a b', 'bssh>'),
        ('mv b c', 'bssh>'),
        ('cat c', 'helloworld'),
        ('stat c', 'Bytes: 10'),
        ('mkdir docs', 'bssh>'),
        ('cd docs', 'bssh>'),
        ('pwd', '/docs'),
        ('cd ..', 'bssh>'),
        ('rmdir docs', 'bssh>'),
        ('df', f'Free inodes: {base_free_inodes - 2} / 64'),
        ('diskinfo', 'Device sectors (512 bytes): 4096'),
        ('sync', 'filesystem saved'),
        ('unmount', 'filesystem unmounted'),
        ('mount', 'no filesystem mounted'),
        ('mount ram0 /', 'mounted ram0 on /'),
    ])
    run(disk, directory, [('cat c', 'helloworld'), ('df', f'Free inodes: {base_free_inodes - 2} / 64')])
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    result = subprocess.run(['debugfs', '-R', 'cat /c', str(disk)], capture_output=True, text=True, check=True)
    assert result.stdout == 'helloworld'
    print('QEMU shell, persistence and ext2 interoperability tests passed')

    run(None, directory, [
        ('diskinfo', 'Device: RAM disk (volatile)'),
        ('write optical hello', 'bssh>'),
        ('cat optical', 'hello'),
        ('mkdir docs', 'bssh>'),
        ('cd docs', 'bssh>'),
        ('pwd', '/docs'),
        ('sync', 'filesystem saved'),
    ])
    run(None, directory, [('stat optical', 'operation failed'),
                          ('df', f'Free inodes: {base_free_inodes} / 64')])
    before = disk.read_bytes()
    run(disk, directory, [('diskinfo', 'Device: RAM disk (volatile)'),
                          ('write live temporary', 'bssh>'),
                          ('cat live', 'temporary')], live_only=True)
    assert disk.read_bytes() == before, 'Live-only mode modified the ATA disk'
    # An unrecognized attached disk must not be formatted by the fallback.
    disk.write_bytes(bytes(2 * 1024 * 1024))
    run(disk, directory, [('diskinfo', 'Device: RAM disk (volatile)'),
                          ('write fallback ram', 'bssh>')])
    assert disk.read_bytes() == bytes(2 * 1024 * 1024)
    print('CD/DVD boot, RAM writes, volatile restart, and ATA isolation tests passed')

with tempfile.TemporaryDirectory(prefix='barnix-exec-') as temporary:
    run(None, pathlib.Path(temporary), [
        ('write bad notelf', 'program exited: 0'),
        ('./bad', 'ELF: truncated ELF header'),
        ('cd bin', 'program exited: 0'),
        ('rm echo', 'program exited: 0'),
        ('echo absent', 'command not found'),
        ('./help', 'run a Barnix ELF32 program'),
        ('clear', 'program exited: 0'),
    ])
