"""Real PS/2 input, VGA glyphs, UTF-8 disk bytes and live language reload in QEMU."""
import pathlib
import shutil
import subprocess
import tempfile
import time

RU_KEYS = dict(zip('йцукенгшщзхъфывапролджэячсмитьбюё',
                   ['q','w','e','r','t','y','u','i','o','p','bracket_left','bracket_right',
                    'a','s','d','f','g','h','j','k','l','semicolon','apostrophe',
                    'z','x','c','v','b','n','m','comma','dot','grave_accent']))
ASCII_KEYS = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', '=': 'equal',
              ',': 'comma', '!': 'shift-1', '#': 'shift-3', '_': 'shift-minus'}


class Guest:
    def __init__(self, directory, disk):
        self.directory, self.ru = directory, False
        self.process = subprocess.Popen([
            'qemu-system-i386', '-m', '256M', '-boot', 'd', '-cdrom', 'barnix.iso',
            '-drive', f'file={disk},format=raw,if=ide', '-display', 'none',
            '-serial', 'none', '-monitor', 'stdio', '-no-reboot'],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        time.sleep(3)
        self.key('ret')
        self.expect('bssh>', 15)

    def monitor(self, command):
        self.process.stdin.write((command + '\n').encode())
        self.process.stdin.flush()

    def key(self, key):
        self.monitor(f'sendkey {key} 1')
        time.sleep(.045)

    def type(self, text):
        for char in text:
            ru = char.lower() in RU_KEYS
            if ru != self.ru:
                self.key('shift-alt')
                self.ru = ru
            if ru:
                key = RU_KEYS[char.lower()]
                if char.isupper(): key = 'shift-' + key
            else:
                key = ASCII_KEYS.get(char, 'shift-' + char.lower() if char.isupper() else char)
            self.key(key)
        if self.ru:
            self.key('alt-shift')
            self.ru = False

    def screen(self):
        path = self.directory / 'vga.bin'
        self.monitor(f'pmemsave 0xb8000 4000 "{path}"')
        time.sleep(.15)
        data = path.read_bytes()[::2]
        return '\n'.join(data[i:i+80].decode('cp866').rstrip() for i in range(0, 2000, 80))

    def expect(self, text, timeout=8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            screen = self.screen()
            if text in screen: return screen
            time.sleep(.15)
        raise AssertionError(f'Missing {text!r}:\n{screen}')

    def command(self, text, expected):
        self.type('clear'); self.key('ret'); time.sleep(.4)
        self.type(text); self.key('ret')
        screen = self.expect(expected)
        self.expect('bssh>')
        print(f'PASS: {text} -> {expected}', flush=True)
        return screen

    def close(self):
        self.monitor('quit')
        try: self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill(); self.process.wait()


with tempfile.TemporaryDirectory(prefix='barnix-lang-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'disk.img'
    shutil.copyfile('live-ext2.img', disk)
    guest = Guest(directory, disk)
    try:
        guest.command('echo Привет, мир! Ёж ёж', '\nПривет, мир! Ёж ёж\n')
        guest.command('mkdir каталог', 'program exited: 0')
        guest.command('cd /каталог', 'program exited: 0')
        guest.command('write текст Привет Ёж', 'program exited: 0')
        guest.command('cat текст', '\nПривет Ёж\n')
        guest.command('write /etc/sys-lang.cfg LANG=ru', 'program exited: 0')
        guest.command('init', 'Настройки применены')
        guest.command('pwd', '\n/каталог\n')
        guest.command('stat текст', 'Байт: 17')
        guest.command('cat текст', '\nПривет Ёж\n')
        guest.command('echo configuration applied', '\nconfiguration applied\n')
        guest.command('write /etc/sys-lang.cfg LANG=xx', 'Программа завершена: 0')
        guest.command('init', 'ожидается LANG=en или LANG=ru')
        guest.command('stat текст', 'Байт: 17')
        guest.command('write /etc/sys-lang.cfg LANG=ru', 'Программа завершена: 0')
        guest.command('clear', 'Программа завершена: 0')
        guest.type('echo АБ'); guest.key('backspace'); guest.type('В'); guest.key('ret')
        guest.expect('\nАВ\n')
        # Echo wraps at column 80; deleting a Cyrillic glyph at column zero
        # must erase the previous cell, not write before VGA memory.
        guest.command('clear', 'Программа завершена: 0')
        prefix = 'x' * 68
        guest.type('echo ' + prefix + 'А'); guest.key('backspace'); guest.type('Б'); guest.key('ret')
        guest.expect('\n' + prefix + 'Б\n')
        guest.command('help', 'применить настройки без перезагрузки')
        # Capture actual pixels as well as glyph codes, for manual font inspection.
        guest.command('echo АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ', '\nАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ\n')
        guest.type('echo абвгдеёжзийклмнопрстуфхцчшщъыьэюя'); guest.key('ret')
        guest.expect('\nабвгдеёжзийклмнопрстуфхцчшщъыьэюя\n')
        guest.monitor('screendump /tmp/barnix-cyrillic.ppm')
        time.sleep(.3)
    finally:
        guest.close()
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True, capture_output=True)
    exported = directory / 'utf8.txt'
    subprocess.run(['debugfs', '-R', f'dump /каталог/текст {exported}', str(disk)],
                   check=True, capture_output=True)
    assert exported.read_bytes() == 'Привет Ёж'.encode('utf-8')
    guest = Guest(directory, disk)
    try:
        guest.expect('Файловая система готова')
        guest.command('stat каталог', 'Тип: каталог')
        guest.command('write /etc/sys-lang.cfg LANG=en', 'Программа завершена: 0')
        guest.command('init', 'configuration applied')
        guest.command('stat каталог', 'Type: directory')
        guest.command('cat /каталог/текст', '\nПривет Ёж\n')
    finally:
        guest.close()
    print('Cyrillic input/output, UTF-8 persistence, reload and boot language passed')
