#!/usr/bin/env python3
"""Format one offline block device for Barnix and mount it on the Linux host."""
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

MAX_BLOCKS = 5 * 1024 * 1024


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, text=True, **kwargs)


def flatten(devices):
    result = []
    for device in devices:
        result.append(device)
        result.extend(flatten(device.get('children', [])))
    return result


def inspect_device(device, inventory):
    devices = flatten(inventory['blockdevices'])
    matches = [d for d in devices if d['path'] == str(device)]
    if len(matches) != 1:
        raise ValueError('Cannot uniquely identify the device in lsblk')
    target = matches[0]
    if target['type'] not in ('disk', 'part', 'loop'):
        raise ValueError('Only a disk, partition or loop device can be formatted')
    affected = {str(device)}
    while True:
        expanded = affected | {d['path'] for d in devices if d.get('pkname') in affected}
        # Also handle lsblk versions that nest children but omit their PKNAME.
        for d in devices:
            if d['path'] in affected:
                expanded.update(child['path'] for child in flatten(d.get('children', [])))
        if expanded == affected:
            break
        affected = expanded
    for d in devices:
        if d['path'] not in affected:
            continue
        if d.get('ro'):
            raise ValueError(f"Read-only device: {d['path']}")
        if any(d.get('mountpoints') or []):
            raise ValueError(f"Device is mounted or used as swap: {d['path']}; unmount it first")
        if d['type'] not in ('disk', 'part', 'loop'):
            raise ValueError(f"Device has an active storage mapping: {d['path']}")
        # Holders catch RAID/device-mapper users even when absent from lsblk's tree.
        holders = Path('/sys/dev/block') / d['maj:min'] / 'holders'
        if holders.is_dir() and any(holders.iterdir()):
            raise ValueError(f"Device is used by another block device: {d['path']}")
    size = int(target['size'])
    if size < 2 * 1024 * 1024:
        raise ValueError('Device must be at least 2 MiB')
    return min(size // 1024, MAX_BLOCKS), target['maj:min']


def resolve_device(request):
    device = Path(request).resolve(strict=True)
    metadata = device.stat()
    if not str(device).startswith('/dev/') or not stat.S_ISBLK(metadata.st_mode):
        raise ValueError(f'Not a block device: {device}')
    return device, metadata


def format_device(request, mount_root=Path('/mnt')):
    if not request.startswith('/dev/') or any(c in request for c in '*?[]\n\r'):
        raise ValueError('Specify exactly one /dev/device, not a wildcard')
    if os.geteuid() != 0:
        raise ValueError('Run: sudo make format /dev/device')
    device, metadata = resolve_device(request)
    for tool in ('lsblk', 'wipefs', 'mke2fs', 'mount'):
        if not shutil.which(tool):
            raise ValueError(f'Missing required tool: {tool}')
    owner = int(os.environ.get('SUDO_UID', '0'))
    group = int(os.environ.get('SUDO_GID', '0'))
    if owner < 0 or group < 0:
        raise ValueError('Invalid sudo owner')

    def inspect():
        inventory = json.loads(run(['lsblk', '--json', '--bytes', '--paths', '--output',
                                    'PATH,PKNAME,TYPE,SIZE,RO,MOUNTPOINTS,MAJ:MIN'], capture_output=True).stdout)
        blocks, identity = inspect_device(device, inventory)
        if identity != f'{os.major(metadata.st_rdev)}:{os.minor(metadata.st_rdev)}':
            raise ValueError('Device identity changed')
        return blocks

    blocks = inspect()
    mountpoint = mount_root / ('barnix-' + device.name)
    if mountpoint.is_symlink():
        raise ValueError(f'Mount point is a symlink: {mountpoint}')
    if mountpoint.exists() and (not mountpoint.is_dir() or any(mountpoint.iterdir()) or os.path.ismount(mountpoint)):
        raise ValueError(f'Mount point must be an empty, unmounted directory: {mountpoint}')
    mountpoint.mkdir(parents=True, exist_ok=True)
    # Recheck immediately before the first destructive operation.
    blocks = inspect()
    print(f'Formatting {device}: ext2, {blocks // 1024} MiB; mounting at {mountpoint}', flush=True)
    run(['wipefs', '--all', '--force', '--', str(device)])
    inodes = max(64, ((blocks + 8191) // 8192) * 128)
    run(['mke2fs', '-E', 'nodiscard', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
         '-N', str(inodes), '-O', 'none,filetype,sparse_super', str(device), str(blocks)])
    try:
        run(['mount', '-t', 'ext2', '-o', 'rw,nosuid,nodev', '--', str(device), str(mountpoint)])
    except subprocess.CalledProcessError as error:
        raise ValueError(f'{device} was formatted, but mounting failed; retry: sudo mount -t ext2 {device} {mountpoint}') from error
    os.chown(mountpoint, owner, group)
    os.chmod(mountpoint, 0o755)
    print(f'Mounted: {device} -> {mountpoint}')


def main():
    arguments = sys.argv[1:] or [os.environ.get('FORMAT_REQUEST', '')]
    if len(arguments) != 1 or not arguments[0]:
        raise ValueError('usage: sudo make format /dev/device')
    format_device(arguments[0])


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'format: {error}', file=sys.stderr)
        sys.exit(1)
