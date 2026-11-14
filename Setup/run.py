#!/usr/bin/env python3
"""Launch Setup or boot its installed destination, using the same disk selection."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import threading

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'scripts'))
from qemu_host import host_options  # noqa: E402
import virtual_wifi  # noqa: E402


def installed(disk, efi):
    """True when the disk has a boot signature and (for UEFI) an EFI System Partition."""
    try:
        with disk.open('rb') as stream:
            mbr = stream.read(512)
    except OSError:
        return False
    if len(mbr) < 512 or mbr[510:] != b'\x55\xaa':
        return False
    return not efi or any(mbr[446 + 16 * i + 4] in (0xef, 0xee) for i in range(4))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installed', action='store_true',
                        help='boot the selected destination without the installer CD; never create a disk')
    parser.add_argument('--gcc', action='store_true',
                        help='boot the installer LiveCD with the GCC C/C++ toolchain')
    parser.add_argument('--disk', action='append', type=Path,
                        help='existing destination image (repeat for up to eleven disks)')
    parser.add_argument('--transport', choices=['ide', 'sata', 'nvme', 'virtio'], default=None,
                        help='controller used for destination images')
    parser.add_argument('--firmware', choices=['legacy', 'efi'], default='legacy',
                        help='select the Legacy BIOS or x64 UEFI installer')
    parser.add_argument('--ovmf-code', type=Path, default=Path('/usr/share/edk2/x64/OVMF_CODE.4m.fd'))
    parser.add_argument('--ovmf-vars', type=Path, default=Path('/usr/share/edk2/x64/OVMF_VARS.4m.fd'))
    args = parser.parse_args()
    efi = args.firmware == 'efi'
    if args.transport is None:
        args.transport = 'virtio' if args.gcc else 'ide'
    iso = HERE.parent / f'barnix-{args.firmware}-setup{"-gcc" if args.gcc else ""}.iso'
    if args.installed and not args.disk and not os.environ.get('SETUP_DISK'):
        target = HERE / ('target-efi.img' if efi else 'target.img')
        if not installed(target, efi):
            # An empty disk has nothing for the firmware to boot ("Boot0003 ... Not Found").
            print(f'{target.name} has no Barnix installed yet: starting the installer '
                  '(ESC there opens the live shell).', flush=True)
            args.installed = False
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
    # OVMF ignores -boot once=d and would try the empty disk, then PXE, before the
    # CD; under UEFI the boot order comes from bootindex, and the NIC gets no PXE ROM.
    command = ['qemu-system-x86_64' if efi else 'qemu-system-i386', '-m', '4096M' if args.gcc else '512M',
               '-boot', 'menu=on' if efi else 'order=c,menu=on' if args.installed else 'order=c,once=d,menu=on',
               '-netdev', 'user,id=net0', '-device', 'e1000,netdev=net0' + (',romfile=' if efi else '')]
    # KVM, cores, a window that captures the mouse, and sound (scripts/qemu_host.py).
    command += host_options(command[0])
    if not args.installed:
        if efi:
            command += ['-drive', f'file={iso},format=raw,media=cdrom,if=none,id=setupcd',
                        '-device', 'ide-cd,drive=setupcd,bus=ide.1,unit=1,bootindex=0']
        else:
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
    # The local package mirror is reachable through QEMU's 10.0.2.2 gateway.
    packages = HERE / 'build/packages'
    package_server = None
    server_thread = None
    if not args.installed and (packages / 'sha256sums').is_file():
        handler = functools.partial(SimpleHTTPRequestHandler, directory=str(packages))
        package_server = ThreadingHTTPServer(('127.0.0.1', 8073), handler)
        server_thread = threading.Thread(target=package_server.serve_forever, daemon=True)
        server_thread.start()
        print('  Package repository: http://10.0.2.2:8073', flush=True)
    # The Wi-Fi card and the network Quemu-Password-root (scripts/virtual_wifi.py).
    access_point = None
    if virtual_wifi.AESCCM and not os.environ.get('BARNIX_NO_WIFI'):
        access_point = virtual_wifi.VirtualAccessPoint(
            log=lambda text: print(f'[Wi-Fi] {text}', file=sys.stderr, flush=True)).start()
        command += access_point.qemu_options()
        print(f'  Wi-Fi: {virtual_wifi.SSID} (password {virtual_wifi.PASSWORD})', flush=True)
    try:
        subprocess.run(command, check=True)
    finally:
        if access_point:
            access_point.stop()
        if package_server:
            package_server.shutdown()
            package_server.server_close()
            server_thread.join(timeout=2)


if __name__ == '__main__':
    main()
