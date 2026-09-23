"""UHCI/USB mass-storage integration test, using temporary disk images only."""
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time


class Guest:
    def __init__(self, directory, disk):
        self.directory = directory
        self.process = subprocess.Popen([
            'qemu-system-i386', '-m', '256M', '-boot', 'd', '-cdrom', 'barnix.iso',
            '-device', 'piix3-usb-uhci,id=uhci',
            '-blockdev', f'driver=file,node-name=flashfile,filename={disk}',
            '-blockdev', 'driver=raw,node-name=flashdisk,file=flashfile',
            '-display', 'none', '-serial', 'none', '-monitor', 'stdio', '-no-reboot'],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        time.sleep(2)
        self.monitor('sendkey ret')
        self.expect('bssh>', 10)

    def monitor(self, text):
        self.process.stdin.write((text + '\n').encode())
        self.process.stdin.flush()

    def screen(self):
        capture = self.directory / 'vga.bin'
        self.monitor(f'pmemsave 0xb8000 4000 "{capture}"')
        time.sleep(.1)
        data = capture.read_bytes()[::2]
        return '\n'.join(data[i:i+80].decode('ascii', errors='replace').rstrip()
                         for i in range(0, 2000, 80))

    def expect(self, expected, timeout=40):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.screen()
            if expected in text:
                return
            time.sleep(.2)
        raise AssertionError(f'Missing {expected!r}:\n{text}')

    def type(self, text):
        for character in text:
            key = {' ': 'spc', '.': 'dot', '/': 'slash'}.get(character, character)
            self.monitor(f'sendkey {key} 1')
            time.sleep(.025)
        self.monitor('sendkey ret 1')
        time.sleep(.15)

    def command(self, command, expected, timeout=40):
        self.type('clear')
        self.type(command)
        self.expect(expected, timeout)
        print(f'PASS: {command} -> {expected}', flush=True)

    def attach(self):
        self.monitor('device_add usb-storage,bus=uhci.0,drive=flashdisk,id=flash')
        time.sleep(.5)

    def detach(self):
        self.monitor('device_del flash')
        time.sleep(.5)

    def close(self):
        if self.process.poll() is None:
            self.monitor('quit')
            self.process.wait(timeout=5)


def check_image(path, start=0):
    filesystem = path.with_suffix('.ext2')
    filesystem.write_bytes(path.read_bytes()[start:start + 2 * 1024 * 1024])
    subprocess.run(['e2fsck', '-fn', str(filesystem)], check=True)
    result = subprocess.run(['debugfs', '-R', 'cat /note', str(filesystem)],
                            check=True, capture_output=True, text=True)
    assert result.stdout == 'usbdata'
    exported = path.with_suffix('.dat')
    subprocess.run(['debugfs', '-R', f'dump /program.dat {exported}', str(filesystem)],
                   check=True, capture_output=True)
    assert exported.read_bytes() == bytes(i % 251 for i in range(32768))


with tempfile.TemporaryDirectory(prefix='barnix-usb-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'usb.img'
    shutil.copyfile('live-ext2.img', disk)
    guest = Guest(directory, disk)
    try:
        guest.command('write ramnote original', 'bssh>')
        guest.command('mount usb0 /', 'mount failed')
        guest.command('mount ram0 /', 'mounted ram0 on /')
        guest.attach()
        guest.command('devices', 'usb0 - USB mass storage')
        guest.command('mount usb0 /', 'mounted usb0 on /')
        guest.command('diskinfo', 'Device: USB mass storage (persistent)')
        guest.command('write note usbdata', 'bssh>')
        guest.command('cat note', 'usbdata')
        guest.command('./hello.elf usbarg', 'usbarg')
        guest.command('./fileio.elf', 'program exited: 0')
        guest.command('unmount', 'filesystem unmounted; device may be removed')
        guest.command('mount', 'no filesystem mounted')
        guest.command('cat note', 'cannot read executable')
        guest.command('unmount', 'unmount failed')
        guest.detach()
        guest.command('mount ram0 /', 'mounted ram0 on /')
        guest.command('cat ramnote', 'original')
        guest.attach()
        guest.command('mount usb0 /', 'mounted usb0 on /')
        guest.command('cat note', 'usbdata')
        guest.detach()
        guest.attach()
        guest.command('devices', 'usb0 - USB mass storage')
        guest.command('cat note', 'device removed; filesystem unmounted')
        guest.command('mount usb0 /', 'mounted usb0 on /')
        guest.detach()
        guest.command('cat note', 'device removed; filesystem unmounted')
        guest.command('mount ram0 /', 'mounted ram0 on /')
    finally:
        guest.close()
    check_image(disk)

    # Same ext2 filesystem inside a primary Linux MBR partition at LBA 2048.
    partitioned = bytearray(4 * 1024 * 1024)
    partitioned[446 + 4] = 0x83
    struct.pack_into('<II', partitioned, 446 + 8, 2048, 4096)
    partitioned[510:512] = b'\x55\xaa'
    partitioned[1024 * 1024:3 * 1024 * 1024] = pathlib.Path('live-ext2.img').read_bytes()
    disk.write_bytes(partitioned)
    guest = Guest(directory, disk)
    try:
        guest.attach()
        guest.command('mount usb0 /', 'mounted usb0 on /')
        guest.command('diskinfo', 'Filesystem start LBA: 2048')
        guest.command('write note usbdata', 'bssh>')
        guest.command('./fileio.elf', 'program exited: 0')
        guest.command('unmount', 'filesystem unmounted; device may be removed')
    finally:
        guest.close()
    assert disk.read_bytes()[:1024 * 1024] == partitioned[:1024 * 1024]
    check_image(disk, 1024 * 1024)
    print('USB hotplug, raw/MBR ext2, ELF, persistence and unmount tests passed')
