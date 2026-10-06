"""USB pointer traffic, idle input and hotplug, including xHCI ring wrap."""
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
from usb_test import Guest


def main():
    symbols = {row.split()[-1]: int(row.split()[0], 16)
               for row in subprocess.check_output(['nm', 'kernel.elf'], text=True).splitlines()
               if len(row.split()) == 3}
    with tempfile.TemporaryDirectory(prefix='barnix-usb-pointer-') as temporary:
        directory = pathlib.Path(temporary)
        disk = directory / 'unused.img'
        shutil.copyfile('live-ext2.img', disk)
        guest = Guest(directory, disk)
        def read(name, fmt):
            path = directory / (name + '.bin')
            guest.monitor(f'pmemsave {symbols[name]:#x} {struct.calcsize(fmt)} "{path}"')
            time.sleep(.08)
            return struct.unpack(fmt, path.read_bytes())
        def state():
            return read('ps2_mouse_state', '<iiIIi')
        def check(predicate, message):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                current = state()
                if predicate(current):
                    return current
            raise AssertionError((message, current))
        try:
            before = state()
            assert before[4]
            guest.monitor('mouse_move 12 -7')
            check(lambda s: s[:2] == (before[0]+12, before[1]-7), 'signed movement')
            for button in (1, 2, 4, 7, 0):
                guest.monitor(f'mouse_button {button}')
                check(lambda s: s[2] == button, f'buttons {button}')
            wheel = read('wheel_total', '<i')[0]
            guest.monitor('mouse_move 0 0 1')
            time.sleep(.2)
            assert abs(read('wheel_total', '<i')[0] - wheel) == 1, 'wheel event lost'
            # More than two complete xHCI transfer ring traversals.
            for n in range(140):
                before = state()
                delta = 1 if n % 2 else -1
                guest.monitor(f'mouse_move {delta} 0')
                check(lambda s: s[0] == before[0]+delta, f'ring wrap at report {n}')
            guest.command('echo idleinput', 'idleinput')
            guest.monitor('mouse_button 1')
            check(lambda s: s[2] == 1, 'press before unplug')
            guest.monitor('device_del usbmouse')
            check(lambda s: s[2] == 0, 'unplug must release buttons')
            guest.monitor('mouse_button 0')
            guest.monitor(f'device_add usb-mouse,bus={guest.mouse_bus},port={guest.mouse_port},id=usbmouse')
            time.sleep(.7)
            before = state()
            guest.monitor('mouse_move -9 11')
            check(lambda s: s[:2] == (before[0]-9, before[1]+11), 'replug movement')
            guest.monitor('mouse_button 2')
            check(lambda s: s[2] == 2, 'replug button')
            guest.monitor('mouse_button 0')
            check(lambda s: not s[2], 'button release')
            guest.command('echo stillworking', 'stillworking')
        finally:
            guest.close()
    print(f"USB pointer: movement, buttons, wheel, 140 reports, idle and hotplug passed ({os.environ.get('USB_HOST', 'uhci')}, hub={bool(os.environ.get('USB_HUB'))})")


if __name__ == '__main__':
    main()
