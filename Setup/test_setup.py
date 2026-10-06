"""Real wizard keyboard flow, disk isolation, ext2 verification and CD-free boot."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import os

ROOT = Path(__file__).resolve().parents[1]


class Guest:
    def __init__(self, directory, disks, setup=True, transport="ide", efi=False, usb_mouse=False):
        self.directory = directory
        self.efi = efi
        kernel = ROOT / ('Setup/kernel-efi.elf' if setup else 'kernel-efi.elf')
        if efi:
            symbols = subprocess.check_output(['nm', str(kernel)], text=True).splitlines()
            self.text_address = next(int(line.split()[0], 16) for line in symbols if line.endswith(' console_text'))
        args = ['qemu-system-x86_64' if efi else 'qemu-system-i386', '-m', '256M', '-display', 'none', '-serial', 'none',
                '-monitor', 'stdio', '-no-reboot']
        if efi:
            variables = directory / 'OVMF_VARS.fd'
            shutil.copyfile('/usr/share/edk2/x64/OVMF_VARS.4m.fd', variables)
            args += ['-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd',
                     '-drive', f'if=pflash,format=raw,file={variables}']
        if setup:
            iso = ROOT / ('barnix-efi-setup.iso' if efi else 'barnix-legacy-setup.iso')
            args += ['-boot', 'd', '-drive', f'file={iso},format=raw,media=cdrom,if=ide,index=3']
        if transport == 'usb':
            args += ['-device', 'qemu-xhci,id=setupusb']
        if transport == 'sata':
            args += ['-device', 'ich9-ahci,id=setupahci']
        for i, disk in enumerate(disks):
            args += ['-drive', f'file={disk},format=raw,if=none,id=disk{i}']
            if transport == 'ide':
                device = f'ide-hd,drive=disk{i},bus=ide.{i // 2},unit={i % 2}'
            elif transport == 'sata':
                device = f'ide-hd,drive=disk{i},bus=setupahci.{i}'
            elif transport == 'usb':
                device = f'usb-storage,drive=disk{i},bus=setupusb.0,port={i+1}'
            elif transport == 'nvme':
                device = f'nvme,drive=disk{i},serial=SETUP{i}'
            else:
                device = f'virtio-blk-pci,drive=disk{i},disable-legacy=on'
            args += ['-device', device + (',bootindex=1' if not setup and i == len(disks) - 1 else '')]
        if usb_mouse:
            args += ['-device', ({'xhci':'qemu-xhci','ohci':'pci-ohci'}.get(os.environ.get('USB_HOST'),'piix3-usb-uhci'))+',id=mouseusb', '-device', 'usb-mouse,bus=mouseusb.0,port=1,id=usbmouse']
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def monitor(self, command):
        self.process.stdin.write((command + '\n').encode())
        self.process.stdin.flush()

    def screen(self):
        path = self.directory / 'screen.bin'
        address = self.text_address if self.efi else 0xb8000
        self.monitor(f'pmemsave {address:#x} 4000 "{path}"')
        time.sleep(.12)
        # OVMF startup can delay the monitor's first memory capture.
        deadline=time.monotonic()+3
        while not path.exists() and time.monotonic()<deadline:
            if self.process.poll() is not None:
                raise AssertionError(self.process.stderr.read().decode())
            time.sleep(.05)
        data = path.read_bytes()[::2]
        return '\n'.join(data[i:i+80].decode('cp866').rstrip() for i in range(0, 2000, 80))

    def expect(self, expected, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError(self.process.stderr.read().decode())
            text = self.screen()
            if expected in text:
                return text
            time.sleep(.2)
        raise AssertionError((expected, text))

    def key(self, key):
        self.monitor(f'sendkey {key} 1')
        time.sleep(.06)

    def type(self, text):
        for c in text:
            self.key({' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus'}.get(c, c))

    def command(self, text, expected):
        self.type('clear'); self.key('ret'); time.sleep(.2)
        self.type(text); self.key('ret')
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            screen = self.screen()
            marker = 'bssh> ' + text
            start = screen.rfind(marker)
            output = screen[start + len(marker):] if start >= 0 else screen
            if expected in output and screen.rstrip().endswith('bssh>'):
                return
            time.sleep(.2)
        raise AssertionError((text, expected, screen))

    def accounts(self, russian=False):
        self.expect('Имя вашего пользователя' if russian else 'Create your username')
        self.type('alice');self.key('ret')
        for title,value in [(('Ваш пароль' if russian else 'Your password'),'alicepw'),
                            (('Повторите пароль' if russian else 'Repeat password'),'alicepw'),
                            (('Отдельный пароль root' if russian else 'Separate root password'),'rootpw'),
                            (('Повторите пароль' if russian else 'Repeat password'),'rootpw')]:
            self.expect(title);self.type(value);self.key('ret')

    def login_root(self):
        text=self.expect('Login:') if 'First start:' not in self.screen() else self.screen()
        if 'First start:' in text:
            for value in ('rootpw','rootpw','alice','alicepw','alicepw'):
                self.type(value);self.key('ret');time.sleep(.4)
        self.expect('Login:');self.type('root');self.key('ret');self.expect('Password:')
        self.type('rootpw');self.key('ret');self.expect('bssh>')
        self.type('cd /');self.key('ret');time.sleep(.2)

    def screenshot(self, name):
        self.monitor(f'screendump "{self.directory / name}"')
        time.sleep(.2)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=10)


def debug(fs, command):
    return subprocess.run(['debugfs', '-R', command, str(fs)], check=True,
                          capture_output=True, text=True).stdout


def main():
    with tempfile.TemporaryDirectory(prefix='barnix-setup-') as temporary:
        directory = Path(temporary)
        untouched = directory / 'disk1.img'
        # A valid but unrelated root makes selecting the wrong disk observable.
        shutil.copyfile(ROOT / 'live-ext2.img', untouched)
        before = untouched.read_bytes()
        target = directory / 'disk2.img'
        with target.open('wb') as stream:
            stream.truncate(16 * 1024 * 1024)
        guest = Guest(directory, [untouched, target])
        try:
            guest.expect('Welcome to Barnix Setup')
            guest.key('down'); guest.expect('Добро пожаловать')
            guest.key('up'); guest.expect('Welcome to Barnix Setup')
            guest.key('ret'); guest.expect('Where should Barnix be installed?')
            guest.key('ret'); guest.expect('Too small')
            assert target.read_bytes() == bytes(16 * 1024 * 1024)
            guest.key('down'); guest.key('ret'); guest.expect('Partitioning mode'); guest.key('ret'); guest.expect('Choose what to install')
            guest.key('down'); guest.key('ret'); guest.expect('Make this system yours')
            for _ in range(6): guest.key('backspace')
            guest.type('testbox'); guest.key('ret'); guest.accounts(); guest.expect('Review and confirm')
            guest.key('ret'); guest.expect('Review and confirm')
            guest.type('wrong'); guest.key('ret'); guest.expect('Review and confirm')
            assert untouched.read_bytes() == before
            assert target.read_bytes() == bytes(16 * 1024 * 1024)
            guest.key('esc'); guest.expect('Make this system yours')
            guest.key('ret'); guest.accounts(); guest.expect('Review and confirm')
            guest.type('disk2'); guest.key('ret')
            guest.expect('Barnix is installed!', timeout=120)
            # Keep screenshots as reviewable build artifacts.
            guest.screenshot('finished.ppm')
            shutil.copyfile(directory / 'finished.ppm', ROOT / 'Setup/build/finished.ppm')
            guest.key('esc'); guest.login_root()
        finally:
            guest.close()
        assert untouched.read_bytes() == before, 'Installer modified unselected disk'
        data = target.read_bytes()
        assert data[510:512] == b'\x55\xaa'
        start, length = struct.unpack_from('<II', data, 454)
        assert (start, length) == (2048, 15 * 2048)
        fs = directory / 'installed.ext2'
        fs.write_bytes(data[start * 512:(start + length) * 512])
        subprocess.run(['e2fsck', '-fn', str(fs)], check=True)
        assert debug(fs, 'cat /etc/hostname') == 'testbox\n'
        assert 'PROFILE=complete' in debug(fs, 'cat /etc/install.cfg')
        assert 'LANG=en' in debug(fs, 'cat /etc/sys-lang.cfg')
        assert 'Inode:' in debug(fs, 'stat /bin/hello.elf')
        assert 'Inode:' in debug(fs, 'stat /boot/kernel.elf')
        # Keep the unrelated disk as primary: BIOS boots the installed secondary
        # disk, then the kernel must select its root by UUID rather than ata0.
        guest = Guest(directory, [untouched, target], setup=False)
        try:
            guest.login_root()
            guest.command('cat /etc/hostname', 'testbox')
            guest.command('cat /etc/install.cfg', 'PROFILE=complete')
            guest.command('devices lsblk', '/\n')
            guest.command('write persisted yes', 'program exited: 0')
        finally:
            guest.close()
        guest = Guest(directory, [target], setup=False)
        try:
            guest.login_root()
            guest.command('cat persisted', 'yes')
        finally:
            guest.close()
        assert untouched.read_bytes() == before
        # Exiting the wizard at Welcome must leave every destination unchanged.
        after = target.read_bytes()
        guest = Guest(directory, [target])
        try:
            guest.expect('Welcome to Barnix Setup'); guest.key('esc'); guest.login_root()
        finally:
            guest.close()
        assert target.read_bytes() == after
        guest = Guest(directory, [target])
        try:
            guest.expect('Welcome to Barnix Setup'); guest.key('down')
            guest.key('ret'); guest.expect('Куда установить Barnix?')
            guest.key('ret'); guest.expect('Способ разметки'); guest.key('ret'); guest.expect('Что установить?')
            guest.key('up'); guest.key('ret'); guest.expect('Настройка системы')
            guest.key('ret'); guest.accounts(russian=True); guest.expect('Проверьте параметры')
            guest.type('disk1'); guest.key('ret'); guest.expect('Barnix установлен!', timeout=120)
        finally:
            guest.close()
        data = target.read_bytes()
        fs.write_bytes(data[1024 * 1024:])
        subprocess.run(['e2fsck', '-fn', str(fs)], check=True)
        assert 'PROFILE=minimal' in debug(fs, 'cat /etc/install.cfg')
        assert debug(fs, 'cat /etc/sys-lang.cfg') == 'LANG=ru\n'
        assert 'Inode:' not in debug(fs, 'stat /bin/hello.elf')
        assert 'Inode:' not in debug(fs, 'stat /bin/git')
        assert 'Inode:' in debug(fs, 'stat /bin/mount')
        guest = Guest(directory, [target], setup=False)
        try:
            guest.login_root(); guest.command('cat /etc/sys-lang.cfg', 'LANG=ru')
        finally:
            guest.close()
        print('Setup wizard, disk isolation, profiles, languages, persistence and CD-free boot passed')


if __name__ == '__main__':
    main()
