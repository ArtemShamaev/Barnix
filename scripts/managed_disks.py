#!/usr/bin/env python3
"""Create persistent extra disks and include them on the next QEMU start."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REGISTRY = Path('disks/registry.json')


def registered():
    return json.loads(REGISTRY.read_text()) if REGISTRY.exists() else []


def main():
    disks = registered()
    if sys.argv[1] == 'create':
        name = os.environ.get('IMG_REQUEST', '')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', name):
            raise SystemExit('usage: make img NAME (letters, digits, _, - and .)')
        if len(disks) >= 10:
            raise SystemExit('Maximum 10 extra disks (2 IDE + 8 VirtIO).')
        try:
            size = int(os.environ.get('IMG_SIZE_MIB', '2'))
        except ValueError:
            raise SystemExit('SIZE_MIB must be an integer from 2 to 5120')
        if not 2 <= size <= 5120:
            raise SystemExit('SIZE_MIB must be from 2 to 5120')
        path = Path('disks') / (name if name.endswith('.img') else name + '.img')
        path.parent.mkdir(exist_ok=True)
        # Exclusive creation prevents overwriting an existing disk.
        try:
            with path.open('xb') as stream:
                stream.truncate(size * 1024 * 1024)
        except FileExistsError:
            raise SystemExit(f'{path} already exists; choose another name')
        try:
            subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024',
                            '-I', '128', '-N', str(64 if size <= 8 else ((size + 7) // 8) * 128),
                            '-O', 'none,filetype,sparse_super', str(path)], check=True)
        except BaseException:
            path.unlink()
            raise
        disks.append(str(path))
        temporary = REGISTRY.with_suffix('.tmp')
        temporary.write_text(json.dumps(disks, indent=2) + '\n')
        temporary.replace(REGISTRY)
        device = f'disk{len(disks) + 1}' if len(disks) <= 2 else f'virtio{len(disks) - 3}'
        print(f'{path}: {size} MiB created; attached as {device} on the next make run')
    elif sys.argv[1] == 'run':
        args = sys.argv[2:]
        for slot, disk in enumerate(disks, 1):
            if not Path(disk).is_file():
                raise SystemExit(f'Registered disk is missing: {disk}')
            if slot <= 2:
                args += ['-drive', f'file={disk},format=raw,if=ide,index={slot}']
            else:
                args += ['-drive', f'file={disk},format=raw,if=none,id=extra{slot}',
                         '-device', f'virtio-blk-pci,drive=extra{slot},disable-legacy=on']
        os.execvp('qemu-system-i386', ['qemu-system-i386', *args])
    else:
        raise SystemExit('expected create or run')


if __name__ == '__main__':
    main()
