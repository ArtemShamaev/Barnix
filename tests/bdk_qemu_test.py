"""Run generated C/C++ BDK applications in Barnix with a real emulated USB mouse."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Setup'))
sys.path.insert(0, str(ROOT / 'scripts'))
from test_setup import Guest
from install_apps import install


class LiveGuest(Guest):
    def type(self, text):
        for character in text:
            if character == '_':
                self.key('shift-minus')
            else:
                super().type(character)

    def __init__(self, directory, disk):
        self.directory = directory
        self.efi = False
        self.process = subprocess.Popen([
            'qemu-system-i386', '-m', '256M', '-display', 'none', '-serial', 'none',
            '-monitor', 'stdio', '-no-reboot', '-boot', 'd',
            '-drive', f'file={ROOT / "barnix.iso"},media=cdrom,if=ide,index=3',
            '-drive', f'file={disk},format=raw,if=ide,index=0',
            '-device', 'piix3-usb-uhci,id=mouseusb',
            '-device', 'usb-mouse,bus=mouseusb.0,port=1,id=usbmouse',
        ], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def main():
    with tempfile.TemporaryDirectory(prefix='barnix-bdk-qemu-') as temporary:
        work = Path(temporary)
        disk = work / 'root.img'
        shutil.copyfile(ROOT / 'live-ext2.img', disk)
        apps_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'BDK/dist/test-apps'
        apps = sorted(apps_dir.glob('*.elf'))
        assert apps and (apps_dir / 'runtime.elf').is_file(), 'run make test-bdk first'
        install(disk, apps)
        guest = LiveGuest(work, disk)
        try:
            time.sleep(2); guest.key('ret')
            guest.expect('First start:', 60)
            for answer in ('rootpw', 'rootpw', 'tester', 'testerpw', 'testerpw'):
                guest.type(answer); guest.key('ret'); time.sleep(.25)
            guest.expect('Login:'); guest.type('root'); guest.key('ret')
            guest.expect('Password:'); guest.type('rootpw'); guest.key('ret'); guest.expect('bssh>')
            for name in ('bca-c.elf', 'bca-cpp.elf'):
                guest.command(name, 'Hello from Barnino Systems BDK!')
            if (apps_dir / 'bca_cs-cs.elf').exists(): guest.command('bca_cs-cs.elf', 'Hello from Barnino Systems BDK C#!')
            if (apps_dir / 'csruntime.elf').exists(): guest.command('csruntime.elf', 'BDK C# runtime OK 18')
            guest.command('runtime.elf', 'BDK destructor OK')
            screen = guest.screen()
            assert 'BDK runtime OK 42' in screen and 'BDK stdio 7' in screen, screen
            assert screen.index('BDK local destructor OK') < screen.index('BDK destructor OK'), screen
            for name in ('bcawms-c.elf', 'bcawms-cpp.elf', 'bcawms_cs-cs.elf'):
                if not (apps_dir / name).exists(): continue
                guest.type(name); guest.key('ret'); guest.expect('mouse demo:')
                first = guest.expect('Buttons: 0')
                before = re.search(r'X: (\d+) Y: (\d+)', first).groups()
                guest.monitor('mouse_move 30 20'); time.sleep(.3)
                after = re.search(r'X: (\d+) Y: (\d+)', guest.screen()).groups()
                assert before != after, 'USB movement not received through BDK API'
                guest.monitor('mouse_button 1'); guest.expect('Left pressed')
                guest.monitor('device_del usbmouse'); guest.expect('Left released')
                guest.monitor('mouse_button 0')
                guest.monitor('device_add usb-mouse,bus=mouseusb.0,port=1,id=usbmouse')
                time.sleep(.5)
                guest.monitor('mouse_button 2'); guest.expect('Buttons: 2')
                guest.monitor('mouse_button 0'); guest.expect('Buttons: 0')
                guest.key('q'); guest.expect('bssh>')
                print(f'BDK USB mouse movement, buttons, hotplug and keyboard passed: {name}', flush=True)
            for name in ('bga-c.elf', 'bga-cpp.elf', 'bga_cs-cs.elf'):
                if not (apps_dir / name).exists(): continue
                guest.type(name); guest.key('ret'); guest.expect('Barnix Executive')
                guest.key('tab'); guest.key('down'); guest.key('ret'); time.sleep(.2)
                guest.key('esc'); guest.expect('bssh>')
                guest.command('whoami', 'root')
                print(f'BDK graphics launch, input and return to shell passed: {name}', flush=True)
            guest.command('whoami', 'root')
        finally:
            guest.close()
        subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    print('BDK C/C++ programs, runtime and USB mouse passed in Barnix/QEMU', flush=True)


if __name__ == '__main__':
    main()
