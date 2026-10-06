"""Install and boot PCI disks; cap an 8 GiB target at 5 GiB without touching its tail."""
import errno
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from test_setup import Guest, debug


def extract(source, destination, start, size):
    # Copy only allocated host extents so a 5 GiB test stays sparse.
    with source.open('rb') as src, destination.open('wb') as dst:
        dst.truncate(size)
        position = start
        while position < start + size:
            try:
                data = os.lseek(src.fileno(), position, os.SEEK_DATA)
            except OSError as error:
                if error.errno == errno.ENXIO:
                    break
                raise
            end = min(os.lseek(src.fileno(), data, os.SEEK_HOLE), start + size)
            if data >= end:
                break
            src.seek(data); dst.seek(data - start)
            while data < end:
                chunk = src.read(min(1024**2, end - data))
                assert chunk
                dst.write(chunk); data += len(chunk)
            position = end


with tempfile.TemporaryDirectory(prefix='barnix-setup-pci-') as temporary:
    directory = Path(temporary)
    for transport, name in [('sata', 'sata0'), ('nvme', 'nvme0n1'), ('virtio', 'virtio0')]:
        target = directory / (name + '.img')
        large = transport == 'virtio'
        size = 8 * 1024**3 if large else 16 * 1024**2
        fs_size = 5 * 1024**3 if large else size - 1024**2
        with target.open('wb') as stream:
            stream.truncate(size)
            if large:
                stream.seek(1024**2 + fs_size)
                stream.write(b'untouched outside the format limit')
        guest = Guest(directory, [target], transport=transport)
        try:
            guest.expect('Welcome to Barnix Setup')
            guest.key('ret'); guest.expect(name)
            guest.key('ret'); guest.expect('Partitioning mode'); guest.key('ret'); guest.expect('Choose what to install')
            guest.key('ret'); guest.expect('Make this system yours')
            guest.key('ret'); guest.accounts(); guest.expect('Review and confirm')
            guest.type(name); guest.key('ret')
            guest.expect('Barnix is installed!', timeout=180)
        finally:
            guest.close()
        with target.open('rb') as stream:
            mbr = stream.read(512)
            assert struct.unpack_from('<II', mbr, 454) == (2048, fs_size // 512)
            if large:
                stream.seek(1024**2 + fs_size)
                assert stream.read(34) == b'untouched outside the format limit'
        guest = Guest(directory, [target], setup=False, transport=transport)
        try:
            guest.login_root()
            guest.command('cat /etc/install.cfg', 'PROFILE=standard')
            guest.command('devices lsblk', name)
            guest.command('write persisted yes', 'program exited: 0')
        finally:
            guest.close()
        fs = directory / (name + '.ext2')
        extract(target, fs, 1024**2, fs_size)
        subprocess.run(['e2fsck', '-fn', str(fs)], check=True)
        assert debug(fs, 'cat /persisted') == 'yes'
        print(f'{transport}: installation, CD-free boot and ext2 verification passed', flush=True)
