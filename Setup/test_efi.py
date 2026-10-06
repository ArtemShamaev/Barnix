"""OVMF: UEFI wizard, ESP/ext2 verification, then boot without CD or saved NVRAM."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile
from test_setup import Guest, debug, ROOT


def extract(source, destination, offset, size):
    with source.open('rb') as src, destination.open('wb') as dst:
        src.seek(offset)
        while size:
            data = src.read(min(size, 1024 * 1024))
            assert data
            if any(data):
                dst.write(data)
            else:
                dst.seek(len(data), 1)
            size -= len(data)
        dst.truncate()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').digest()


for mode, platform in [('legacy', 'BIOS'), ('efi', 'UEFI')]:
    report = subprocess.run(['xorriso', '-indev', str(ROOT / f'barnix-{mode}-setup.iso'),
                             '-report_el_torito', 'plain'], check=True, capture_output=True, text=True)
    entries = [line for line in (report.stdout + report.stderr).splitlines() if line.startswith('El Torito boot img')]
    assert len(entries) == 1 and platform in entries[0], entries


with tempfile.TemporaryDirectory(prefix='barnix-uefi-') as temporary:
    directory = Path(temporary)
    unrelated = directory / 'unrelated.img'
    shutil.copyfile(ROOT / 'live-ext2.img', unrelated)
    untouched = digest(unrelated)
    target = directory / 'target.img'
    with target.open('wb') as stream:
        stream.truncate(128 * 1024 * 1024)
        # Reinstall onto nonzero data: zero-filled allocated ESP tails must be
        # written too, while unallocated clusters need not be erased.
        for _ in range(4): stream.write(b'\xa5' * 1024**2)
    blank = digest(target)
    guest = Guest(directory, [unrelated, target], efi=True)
    try:
        guest.expect('Welcome to Barnix Setup', timeout=40)
        guest.screenshot('efi-welcome.ppm')
        shutil.copyfile(directory / 'efi-welcome.ppm', ROOT / 'Setup/build/efi-welcome.ppm')
        guest.key('down'); guest.expect('Добро пожаловать')
        guest.key('up'); guest.key('ret'); guest.expect('Where should Barnix be installed?')
        guest.key('ret'); guest.expect('Too small')
        guest.key('down'); guest.key('ret'); guest.expect('Partitioning mode'); guest.key('ret'); guest.expect('Choose what to install')
        guest.key('down'); guest.key('ret'); guest.expect('Make this system yours')
        for _ in range(6): guest.key('backspace')
        guest.type('efibox'); guest.key('ret'); guest.accounts(); guest.expect('Review and confirm')
        guest.key('ret'); guest.expect('Review and confirm')
        assert digest(target) == blank
        guest.type('disk2'); guest.key('ret'); guest.expect('Barnix is installed!', timeout=180)
        guest.screenshot('efi-finished.ppm')
        shutil.copyfile(directory / 'efi-finished.ppm', ROOT / 'Setup/build/efi-finished.ppm')
        guest.key('esc'); guest.login_root()
        guest.command('echo framebuffer works', 'framebuffer works')
    finally:
        guest.close()
    assert digest(unrelated) == untouched
    with target.open('rb') as stream:
        mbr = stream.read(512)
    assert mbr[510:] == b'\x55\xaa' and mbr[450] == 0xef and mbr[466] == 0x83
    assert struct.unpack_from('<II', mbr, 454) == (2048, 64 * 2048)
    assert struct.unpack_from('<II', mbr, 470) == (65 * 2048, 63 * 2048)
    esp = directory / 'esp.img'
    extract(target, esp, 1024**2, 64 * 1024**2)
    subprocess.run(['fsck.fat', '-n', str(esp)], check=True)
    boot = directory / 'BOOTX64.EFI'
    subprocess.run(['mcopy', '-i', str(esp), '::/EFI/BOOT/BOOTX64.EFI', str(boot)], check=True)
    assert boot.read_bytes() == (ROOT / 'Setup/build/BOOTX64.EFI').read_bytes()
    for transport in ('ide', 'nvme', 'virtio', 'sata'):
        disks = [unrelated, target] if transport == 'ide' else [target]
        # Guest creates fresh OVMF variables each time: fallback boot must work.
        guest = Guest(directory, disks, setup=False, transport=transport, efi=True)
        try:
            guest.login_root()
            guest.command('cat /etc/hostname', 'efibox')
            guest.command('write persisted uefi', 'program exited: 0')
            guest.command('cat persisted', 'uefi')
            guest.command('./hello.elf efi', 'efi')
            guest.screenshot('efi-shell.ppm')
            shutil.copyfile(directory / 'efi-shell.ppm', ROOT / 'Setup/build/efi-shell.ppm')
        finally:
            guest.close()
        print(f'UEFI disk boot without CD/NVRAM: {transport} passed', flush=True)
    fs = directory / 'root.ext2'
    extract(target, fs, 65 * 1024**2, 63 * 1024**2)
    subprocess.run(['e2fsck', '-fn', str(fs)], check=True)
    assert debug(fs, 'cat /persisted') == 'uefi'
    assert 'PROFILE=complete' in debug(fs, 'cat /etc/install.cfg')
    assert digest(unrelated) == untouched
    print('UEFI installation, FAT32 ESP, GOP console, root UUID, disk isolation and persistence passed')
