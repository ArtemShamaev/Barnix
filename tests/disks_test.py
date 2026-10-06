"""Boot with three ATA disks; exercise guest formatting and mounted paths."""
import pathlib
import shutil
import subprocess
import tempfile
from qemu_shell import run

with tempfile.TemporaryDirectory(prefix='barnix-disks-') as tmp:
    directory = pathlib.Path(tmp)
    root = directory / 'root.img'
    shutil.copyfile('live-ext2.img', root)
    extras = [directory / f'disk{i}.img' for i in (2, 3)]
    for disk in extras:
        disk.write_bytes(bytes(2 * 1024 * 1024))
    run(root, directory, [
        ('devices', 'disk3 - ATA disk'),
        ('devices lsblk', 'disk2   disk 2048      -       -         -         -'),
        ('format disk2', 'formatted: empty ext2 filesystem'),
        ('format disk3', 'formatted: empty ext2 filesystem'),
        ('mkdir one', ''), ('mkdir two', ''),
        ('mount disk2 /one', 'on /one (ext2)'),
        ('mount disk3 /two', 'on /two (ext2)'),
        ('format disk2', 'format failed'),
        ('write /one/note first', ''), ('write /two/note second', ''),
        ('devices lsblk', 'disk2   disk 2048      ext2    23        2025      /one'),
        ('cat /one/note', 'first'), ('cat /two/note', 'second'),
        ('cd /one', ''), ('pwd', '/one'),
        ('cat ../two/note', 'second'),
        ('cd ..', ''), ('pwd', '/'),
        ('cp /one/note /two/copy', ''), ('cat /two/copy', 'first'),
        ('unmount /one', 'filesystem unmounted'),
        ('devices lsblk', 'disk2   disk 2048      ext2    23        2025      -'),
        ('mount disk2 /one', 'on /one (ext2)'),
        ('cat /one/note', 'first'),
        ('sync', ''),
    ], extra_disks=extras, command_timeout=30)
    for disk in extras:
        subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    print('Three ATA disks, format and mount integration passed')
