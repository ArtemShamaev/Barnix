"""Boots the ISO with the virtual Wi-Fi card (scripts/virtual_wifi.py) and an
e1000 cable, and drives bnm: list networks, join Quemu-Password-root with
WPA2, ping the gateway through it, turn networking off, a wrong password."""
import pathlib
import shutil
import sys
import tempfile

from qemu_shell import run

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'scripts'))
from virtual_wifi import VirtualAccessPoint, SSID, PASSWORD  # noqa: E402

ap = VirtualAccessPoint(log=lambda text: print('[Wi-Fi]', text)).start()
try:
    with tempfile.TemporaryDirectory(prefix='barnix-wifi-') as temporary:
        directory = pathlib.Path(temporary)
        disk = directory / 'test.img'
        shutil.copyfile('live-ext2.img', disk)
        run(disk, directory, [
            ('clear', 'bssh>'),
            ('bnm get-networks', SSID),
            ('clear', 'bssh>'),
            (f'bnm connect {SSID} {PASSWORD}', 'Address:  192.168.77.'),
            ('clear', 'bssh>'),
            ('bnm', 'connected to ' + SSID),
            ('bnm test-ping', 'reply in'),
            ('clear', 'bssh>'),
            ('bnm off', 'Networking is off'),
            ('bnm test-ping', 'network is unavailable'),
            ('clear', 'bssh>'),
            (f'bnm connect {SSID} wrongpass', 'wrong Wi-Fi password'),
            ('clear', 'bssh>'),
            ('bnm internet', 'Address:  10.0.2.'),
        ], command_timeout=30, extra_args=['-netdev', 'user,id=net0', '-device', 'e1000,netdev=net0'] + ap.qemu_options())
finally:
    ap.stop()
print('Wi-Fi in QEMU: get-networks, WPA2 connect, ping, off, wrong password passed')
