#!/usr/bin/env python3
"""Launch Setup or boot its installed destination, using the same disk selection."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installed', action='store_true',
                        help='boot the selected destination without the installer CD; never create a disk')
    parser.add_argument('--disk', action='append', type=Path,
                        help='existing destination image (repeat for up to eleven disks)')
    parser.add_argument('--transport', choices=['ide', 'sata', 'nvme', 'virtio'], default='ide',
                        help='controller used for destination images')
    parser.add_argument('--firmware', choices=['legacy', 'efi'], default='legacy',
                        help='select the Legacy BIOS or x64 UEFI installer')
    parser.add_argument('--ovmf-code', type=Path, default=Path('/usr/share/edk2/x64/OVMF_CODE.4m.fd'))
    parser.add_argument('--ovmf-vars', type=Path, default=Path('/usr/share/edk2/x64/OVMF_VARS.4m.fd'))
    args = parser.parse_args()
    efi = args.firmware == 'efi'
    iso = HERE.parent / f'barnix-{args.firmware}-setup.iso'
    if not args.installed and not iso.is_file():
        parser.error(f'{iso} is missing; run make setup')
    if efi and (not args.ovmf_code.is_file() or not args.ovmf_vars.is_file()):
        parser.error('OVMF firmware is missing; provide --ovmf-code and --ovmf-vars')
    disks = args.disk or ([Path(os.environ['SETUP_DISK'])] if os.environ.get('SETUP_DISK') else None)
    if not disks:
        target = HERE / ('target-efi.img' if efi else 'target.img')
        if not args.installed:
            try:
                with target.open('xb') as stream:
                    stream.truncate(5 * 1024**3 + (65 if efi else 1) * 1024**2)
            except FileExistsError:
                pass
        disks = [target]
    limit = {'ide': 11, 'sata': 6, 'nvme': 8, 'virtio': 8}[args.transport]
    if len(disks) > limit:
        parser.error(f'at most {limit} disks for this transport')
    for disk in disks:
        if not disk.is_file():
            parser.error(f'disk does not exist: {disk}; run the installer first or select SETUP_DISK')
        if ',' in str(disk):
            parser.error('disk names containing commas are not supported by this launcher')
    command = ['qemu-system-x86_64' if efi else 'qemu-system-i386', '-m', '256M',
               '-boot', 'order=c,menu=on' if args.installed else 'order=c,once=d,menu=on',
               '-netdev', 'user,id=net0', '-device', 'e1000,netdev=net0']
    if not args.installed:
        command += ['-drive', f'file={iso},format=raw,media=cdrom,if=ide,index=3']
    if efi:
        variables = HERE / 'OVMF_VARS.fd'
        if not variables.exists():
            shutil.copyfile(args.ovmf_vars, variables)
        command += ['-drive', f'if=pflash,format=raw,readonly=on,file={args.ovmf_code.resolve()}',
                    '-drive', f'if=pflash,format=raw,file={variables}']
    if args.transport == 'sata':
        command += ['-device', 'ich9-ahci,id=setupahci']
    for index, disk in enumerate(disks):
        if args.transport == 'ide' and index < 3:
            command += ['-drive', f'file={disk.resolve()},format=raw,if=ide,index={index}']
        else:
            command += ['-drive', f'file={disk.resolve()},format=raw,if=none,id=target{index}']
            if args.transport == 'sata':
                device = f'ide-hd,drive=target{index},bus=setupahci.{index}'
            elif args.transport == 'nvme':
                device = f'nvme,drive=target{index},serial=BARNIX{index}'
            else:
                device = f'virtio-blk-pci,drive=target{index},disable-legacy=on'
            command += ['-device', device]
    print(('Boot installed system' if args.installed else 'Installer') + f' ({args.firmware}):', flush=True)
    for index, disk in enumerate(disks, 1):
        print(f'  Target {index}: {disk.resolve()}', flush=True)
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
