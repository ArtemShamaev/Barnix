#!/usr/bin/env python3
"""Register an ELF for Complete and import it into offline disks/*.img."""
from contextlib import ExitStack
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from install_apps import install

ROOT = Path(__file__).resolve().parents[1]


def validate_name(name):
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.elf', name) or len(name) > 23:
        raise ValueError('ELF filename must end in .elf, contain only letters, digits, _, -, . and be at most 23 characters')


def complete_apps(root):
    """Stable copies remain available even after the original project is removed."""
    paths = sorted((root / 'Setup/extra-apps').glob('*.elf'))
    manifest = json.loads((root / 'Setup/profiles.json').read_text())
    reserved = {name for p in manifest['profiles'] for field in ('commands', 'examples') for name in p.get(field, [])}
    for path in paths:
        validate_name(path.name)
        if path.name in reserved:
            raise ValueError(f'{path.name} conflicts with a bundled application')
    return paths


def filesystem_extent(image):
    """Raw ext2 or one Linux primary partition, preserving all other disk bytes."""
    size = image.stat().st_size
    with image.open('rb') as stream:
        stream.seek(1080)
        if stream.read(2) == b'\x53\xef':
            return 0, size
        stream.seek(0)
        mbr = stream.read(512)
        if len(mbr) != 512 or mbr[510:] != b'\x55\xaa':
            raise ValueError(f'{image.name}: expected an ext2 image or MBR Linux partition')
        partitions = []
        for index in range(4):
            entry = mbr[446 + index * 16:462 + index * 16]
            if entry[4] == 0x83:
                start, length = struct.unpack_from('<II', entry, 8)
                start *= 512
                length *= 512
                if start < 512 or length < 2048 or start + length > size:
                    raise ValueError('Invalid Linux partition bounds')
                stream.seek(start + 1080)
                if stream.read(2) != b'\x53\xef':
                    raise ValueError('Linux partition is not ext2')
                partitions.append((start, length))
        if len(partitions) != 1:
            raise ValueError('Expected exactly one Linux primary partition')
        start, length = partitions[0]
        for index in range(4):
            entry = mbr[446 + index * 16:462 + index * 16]
            if entry[4] and entry[4] != 0x83:
                other, count = struct.unpack_from('<II', entry, 8)
                if max(start, other * 512) < min(start + length, (other + count) * 512):
                    raise ValueError('Overlapping partitions')
        return partitions[0]


def import_image(image, program, temporary):
    offset, length = filesystem_extent(image)
    if offset == 0:
        install(image, [program])
        return
    partition = temporary / 'partition.ext2'
    with image.open('rb') as source, partition.open('wb') as target:
        source.seek(offset)
        remaining = length
        while remaining:
            chunk = source.read(min(remaining, 1024 * 1024))
            if not chunk:
                raise ValueError('Truncated partition')
            target.write(chunk)
            remaining -= len(chunk)
    install(partition, [program])
    with image.open('r+b') as target, partition.open('rb') as source:
        target.seek(offset)
        shutil.copyfileobj(source, target)
    partition.unlink()


def add(program, root=ROOT):
    program = Path(program).resolve()
    validate_name(program.name)
    if not program.is_file():
        raise ValueError(f'ELF does not exist: {program}')
    manifest = json.loads((root / 'Setup/profiles.json').read_text())
    if not any(p['id'] == 'complete' for p in manifest['profiles']):
        raise ValueError('Complete profile is missing')
    reserved = {n for p in manifest['profiles'] for key in ('commands', 'examples') for n in p.get(key, [])}
    if program.name in reserved:
        raise ValueError(f'{program.name} conflicts with a bundled application')
    with ExitStack() as stack:
        work = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='barnix-add-elf-')))
        saved = work / program.name
        shutil.copyfile(program, saved)
        validator = work / 'validate-elf'
        subprocess.run(['cc', '-std=c11', '-I', str(ROOT), str(ROOT / 'scripts/validate_elf.c'),
                        str(ROOT / 'elf_format.c'), '-o', str(validator)], check=True)
        subprocess.run([str(validator), str(saved)], check=True)
        directory = root / 'Setup/extra-apps'
        directory.mkdir(exist_ok=True)
        # Serialize imports; QEMU's image byte-range locks also conflict with lockf.
        lock = stack.enter_context((directory / '.lock').open('a+b'))
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        pending = []
        for disk in sorted((root / 'disks').glob('*.img')):
            if disk.is_symlink() or not disk.is_file():
                raise ValueError(f'Not a regular disk image: {disk}')
            handle = stack.enter_context(disk.open('r+b'))
            try:
                fcntl.lockf(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as error:
                raise ValueError(f'Disk is in use; stop QEMU first: {disk}') from error
            temporary = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='.add-elf-', dir=disk.parent)))
            staged = temporary / disk.name
            backup = temporary / 'original.img'
            shutil.copy2(disk, staged)
            import_image(staged, saved, temporary)
            pending.append((disk, staged, backup))
        temporary = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='.add-elf-', dir=directory)))
        staged = temporary / program.name
        shutil.copy2(saved, staged)
        pending.append((directory / program.name, staged, temporary / 'original.elf'))
        committed = []
        try:
            for destination, staged, backup in pending:
                existed = destination.exists()
                if existed:
                    os.link(destination, backup)
                os.replace(staged, destination)
                committed.append((destination, backup, existed))
        except BaseException:
            for destination, backup, existed in reversed(committed):
                if existed:
                    os.replace(backup, destination)
                else:
                    destination.unlink()
            raise
        print(f'Complete: {directory / program.name} (included by the next make setup)')
        for disk, _, _ in pending[:-1]:
            print(f'{disk}: /bin/{program.name} and /{program.name}')
        if len(pending) == 1:
            print('No disks/*.img found; ELF registered for Complete only.')


def main():
    arguments = sys.argv[1:]
    if not arguments:
        arguments = [os.environ.get('ADD_ELF_REQUEST', '')]
    if len(arguments) != 1 or not arguments[0]:
        raise ValueError('usage: make add_elf /path/to/app.elf (or make add_elf ELF="path with spaces/app.elf")')
    add(arguments[0])


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'add_elf: {error}', file=sys.stderr)
        sys.exit(1)
