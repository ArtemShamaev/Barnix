"""Boots the ISO, starts Happy Gouse and Gouse Web from the Gouse menu, and
checks the screen: the home page's blue header, then a file: folder listing
with its links. Works offline (no network pages)."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'scripts'))
from qemu_host import host_options  # noqa: E402

os.environ['BARNIX_NO_AUDIO'] = '1'
os.environ['QEMU_DEFAULT_DISPLAY'] = '1'
keys = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus', ':': 'shift-semicolon'}

with tempfile.TemporaryDirectory(prefix='barnix-gouse-web-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'test.img'
    shutil.copyfile('live-ext2.img', disk)
    process = subprocess.Popen(
        ['qemu-system-i386', '-m', '512M', '-boot', 'd', '-drive', 'file=barnix.iso,media=cdrom,if=ide,index=3',
         '-drive', f'file={disk},format=raw,if=ide', '-display', 'none', '-serial', 'none', '-monitor', 'stdio', '-no-reboot']
        + host_options('qemu-system-i386'), stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def monitor(command):
        process.stdin.write((command + '\n').encode())
        process.stdin.flush()

    def type_text(text):
        for ch in text:
            monitor('sendkey ' + ('shift-' + ch.lower() if ch.isupper() else keys.get(ch, ch)) + ' 1')
            time.sleep(0.03)

    def key(name, pause=0.3):
        monitor(f'sendkey {name} 1')
        time.sleep(pause)

    def screen(name):
        path = directory / f'{name}.ppm'
        monitor(f'screendump {path}')
        for _ in range(50):
            time.sleep(0.2)
            if path.exists() and path.stat().st_size > 1000:
                break
        time.sleep(0.3)
        return Image.open(path).convert('RGB')

    def share(image, box, test):
        data = image.crop(box).tobytes()
        pixels = [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]
        return sum(1 for p in pixels if test(p)) / max(1, len(pixels))

    def text():
        path = directory / 'vga.bin'
        monitor(f'pmemsave 0xb8000 4000 "{path}"')
        time.sleep(0.15)
        try:
            return path.read_bytes()[::2].decode('cp437', errors='replace')
        except FileNotFoundError:
            return ''

    try:
        # Barnix boots within seconds: Enter only while GRUB's menu is shown,
        # or it becomes the first password.
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            shown = text()
            if 'New root password' in shown or 'First start' in shown:
                break
            if 'RAM only' in shown:
                key('ret', 1)
        for answer in ('rootpw', 'rootpw', 'tester', 'testerpw', 'testerpw'):
            type_text(answer)
            key('ret', 0.5)
        type_text('root'); key('ret'); type_text('rootpw'); key('ret', 1)
        type_text('gouse'); key('ret', 8)
        key('ctrl-esc'); key('down'); key('down'); key('ret', 6)
        image = screen('home')
        w, h = image.size
        blue = share(image, (60, 95, min(w - 60, 700), 150), lambda p: p[2] > 150 and p[2] > p[0] + 60)
        assert blue > 0.6, ('the home page header is missing', blue)
        key('ctrl-l'); type_text('file:///'); key('ret', 5)
        image = screen('files')
        if os.environ.get('KEEP_SCREEN'): image.save(os.environ['KEEP_SCREEN'])
        links = share(image, (40, 80, min(w - 40, 400), min(h - 60, 400)), lambda p: p[2] > 180 and p[0] < 80 and p[1] < 80)
        assert links > 0.004, ('no folder links on the file: page', links)
        print('Gouse Web in QEMU: home page and file: listing passed')
        monitor('quit')
        process.wait(timeout=10)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
