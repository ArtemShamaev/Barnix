"""Exercise AHCI, NVMe namespaces/4K sectors, and modern/legacy VirtIO together."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from qemu_shell import run

with tempfile.TemporaryDirectory(prefix='barnix-storage-pci-') as temporary:
    directory = Path(temporary)
    paths = {}
    for name in ('sata0', 'sata1', 'nvme0n1', 'nvme0n2', 'virtio0', 'virtio1', 'usb0', 'usb1', 'usb2'):
        paths[name] = directory / (name + '.img')
        shutil.copyfile('live-ext2.img', paths[name])
    args = ['-device', 'ich9-ahci,id=ahci']
    for i in range(2):
        name = f'sata{i}'
        args += ['-drive', f'file={paths[name]},format=raw,if=none,id={name}',
                 '-device', f'ide-hd,drive={name},bus=ahci.{i}']
    args += ['-device', 'nvme,id=nvm,serial=BARNIXTEST']
    for i in range(2):
        name = f'nvme0n{i+1}'
        args += ['-drive', f'file={paths[name]},format=raw,if=none,id={name}',
                 '-device', f'nvme-ns,drive={name},bus=nvm,nsid={i+1},logical_block_size={512 if i == 0 else 4096},physical_block_size={512 if i == 0 else 4096}']
    for i in range(2):
        name = f'virtio{i}'
        args += ['-drive', f'file={paths[name]},format=raw,if=none,id={name}',
                 '-device', f'virtio-blk-pci,drive={name},disable-{"legacy" if i == 0 else "modern"}=on']
    args += ['-device', 'piix3-usb-uhci,id=usbtest0', '-device', 'piix3-usb-uhci,id=usbtest1']
    for i in range(3):
        name = f'usb{i}'
        args += ['-drive', f'file={paths[name]},format=raw,if=none,id={name}',
                 '-device', f'usb-storage,drive={name},bus=usbtest{i // 2}.0,port={i % 2 + 1}']
    large = directory / 'large.img'
    huge = directory / 'huge.img'
    for path, size in [(large, 8 * 1024**3), (huge, 3 * 1024**4)]:
        with path.open('wb') as stream:
            stream.truncate(size)
    with large.open('r+b') as stream:
        stream.seek(5 * 1024**3); stream.write(b'preserve tail')
    for i, path in enumerate([large, huge], 2):
        args += ['-drive', f'file={path},format=raw,if=none,id=virtio{i}',
                 '-device', f'virtio-blk-pci,drive=virtio{i},disable-legacy=on']
    commands = [('devices lsblk', '3221225472')]

    for name in paths:
        commands += [('mount ' + name + ' /', 'mounted ' + name + ' on /'),
                     ('write probe ' + name, 'program exited: 0'),
                     ('cat probe', name), ('mount ram0 /', 'mounted ram0 on /')]
    commands += [('mkdir data', 'program exited: 0'),
                 ('format virtio2', 'formatted: empty ext2 filesystem'),
                 ('mount virtio2 /data', 'on /data (ext2)'),
                 ('write /data/probe large', 'program exited: 0'),
                 ('cat /data/probe', 'large'),
                 ('devices lsblk', '8388608'),
                 ('unmount /data', 'filesystem unmounted')]
    run(None, directory, commands, extra_args=args, command_timeout=60)
    for name, path in paths.items():
        subprocess.run(['e2fsck', '-fn', str(path)], check=True)
        result = subprocess.run(['debugfs', '-R', 'cat /probe', str(path)], check=True, capture_output=True, text=True)
        assert result.stdout == name
    subprocess.run(['e2fsck', '-fn', str(large)], check=True)
    with large.open('rb') as stream:
        stream.seek(1028); assert int.from_bytes(stream.read(4), 'little') == 5 * 1024 * 1024
        stream.seek(5 * 1024**3); assert stream.read(13) == b'preserve tail'
    print('AHCI, NVMe 512/4096-byte namespaces, modern/legacy VirtIO, multiple USB, 5 GiB format and 3 TiB capacity passed')
