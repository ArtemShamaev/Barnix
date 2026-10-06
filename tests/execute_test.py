"""End-to-end Execute desktop: file operations, launch/restart, mouse and errors."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from bdk_qemu_test import LiveGuest, ROOT


def debug(disk, command):
    result = subprocess.run(['debugfs', '-w', '-R', command, str(disk)],
                            check=True, text=True, capture_output=True)
    return result.stdout


def click(g, x, y):
    symbols = subprocess.check_output(['nm', str(ROOT / 'kernel.elf')], text=True)
    address = next(int(line.split()[0], 16) for line in symbols.splitlines()
                   if line.endswith(' ps2_mouse_state'))
    for _ in range(20):
        path = g.directory / 'pointer.bin'
        g.monitor(f'pmemsave {address:#x} 20 "{path}"'); time.sleep(.08)
        px, py, buttons, sequence, available = struct.unpack('<iiIIi', path.read_bytes())
        assert available
        if (px, py) == (x, y): break
        g.monitor(f'mouse_move {max(-100, min(100, x-px))} {max(-100, min(100, y-py))}')
        time.sleep(.08)
    else:
        raise AssertionError('Mouse did not reach target')
    g.monitor('mouse_button 1'); time.sleep(.15)
    g.monitor('mouse_button 0'); time.sleep(.15)


def main():
    with tempfile.TemporaryDirectory(prefix='barnix-execute-') as temporary:
        work = Path(temporary)
        disk = work / 'root.img'
        shutil.copyfile(ROOT / 'live-ext2.img', disk)
        debug(disk, 'mkdir /work')
        document = work / 'document.txt'
        document.write_text('EXECUTE text preview works.\nSecond line.\n', encoding='utf-8')
        for source, dest, mode in ((document, 'a.txt', '0100644'),
                                   (ROOT / 'apps/bin/true', 'linux', '0100755'),
                                   (ROOT / 'apps/hello.elf', 'native', '0100755'),
                                   (ROOT / 'apps/hello.elf', 'denied', '0100644'),
                                   (ROOT / 'apps/hello.elf', 'execute-only', '0100111')):
            debug(disk, f'write "{source}" /work/{dest}')
            debug(disk, f'set_inode_field /work/{dest} mode {mode}')
        # Old ABI entry points must receive their own version while using the
        # unchanged API prefix on the ABI 9 kernel.
        for version in (7, 8):
            source = work / f'compat{version}.c'
            binary = work / f'compat{version}'
            source.write_text('#include "app_abi.h"\n'
                              'int _start(const BarnixAPI *api) {'
                              f'if(api->version!={version})return 126;'
                              f'api->puts("Compatibility ABI {version} OK");return 0;}}')
            subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie', '-Wl,--build-id=none',
                            '-Wl,-T,' + str(ROOT / 'apps/app.ld'), '-I', str(ROOT),
                            str(source), '-o', str(binary)], check=True)
            data = bytearray(binary.read_bytes()); data[7:9] = bytes((255, version)); binary.write_bytes(data)
            debug(disk, f'write "{binary}" /bin/compat{version}')
            debug(disk, f'set_inode_field /bin/compat{version} mode 0100755')
        g = LiveGuest(work, disk)
        try:
            time.sleep(2); g.key('ret'); g.expect('First start:', 60)
            for answer in ('rootpw', 'rootpw', 'tester', 'testerpw', 'testerpw'):
                g.type(answer); g.key('ret'); time.sleep(.25)
            g.expect('Login:'); g.type('root'); g.key('ret')
            g.expect('Password:'); g.type('rootpw'); g.key('ret'); g.expect('bssh>')
            for version in (7, 8):
                g.command(f'compat{version}', f'Compatibility ABI {version} OK')
            g.command('cd /work', 'program exited: 0')
            g.type('execute'); g.key('ret'); g.expect('Barnix Execute - /work')
            g.key('v'); g.expect('Text viewer'); g.expect('EXECUTE text preview works.')
            g.key('esc'); g.expect('Barnix Execute - /work')
            g.key('n'); g.expect('Create directory'); g.type('alpha'); g.key('ret')
            g.expect('Operation completed.'); g.expect('[DIR]  alpha')
            g.key('ret'); g.expect('Barnix Execute - /work/alpha')
            g.key('backspace'); g.expect('[DIR]  alpha')
            g.key('down'); g.key('c'); g.expect('Copy file'); g.type('b.txt'); g.key('ret')
            g.expect('Operation completed.'); g.expect('b.txt')
            g.key('m'); g.expect('Rename / move')
            for _ in range(5): g.key('backspace')
            g.type('c.txt'); g.key('ret'); g.expect('Operation completed.'); g.expect('c.txt')
            g.key('d'); g.expect('Delete permanently?'); g.key('esc'); g.expect('c.txt')
            g.key('d'); g.expect('Delete permanently?'); g.key('ret'); g.expect('Operation completed.')
            assert 'c.txt' not in g.screen()
            # Sorted entries: alpha, b.txt, denied, execute-only, linux, native.
            g.key('home'); g.key('down'); g.key('down'); g.key('ret')
            g.expect('Cannot run this program: permission denied')
            g.key('down'); g.key('ret'); g.expect('Hello from GCC ELF!'); g.expect('Program exited: 7')
            g.key('spc'); g.expect('Barnix Execute - /work'); g.expect('[DIR]  alpha')
            g.key('home')
            for _ in range(5): g.key('down')
            g.key('ret'); g.expect('Hello from GCC ELF!'); g.expect('Program exited: 7')
            g.key('spc'); g.expect('Barnix Execute - /work')
            # Launch again: validates reloading data/BSS and reusable kernel stack.
            for _ in range(5): g.key('down')
            g.key('ret'); g.expect('ELF data/BSS verified'); g.expect('Program exited: 7')
            g.key('spc'); g.expect('Barnix Execute - /work')
            click(g, 100, 128)  # Select b.txt with USB mouse.
            click(g, 70, 60)    # Open selected text file.
            g.expect('Text viewer'); g.expect('EXECUTE text preview works.')
            g.key('esc'); g.expect('Barnix Execute - /work')
            click(g, 490, 350); g.expect('About Barnix Execute')
            g.key('esc'); g.expect('Barnix Execute - /work')
            g.screenshot('execute.ppm')
            g.key('esc'); g.expect('bssh>'); g.command('pwd', '/work')
            g.command('cat b.txt', 'EXECUTE text preview works.')
            g.command('su tester', 'bssh>')
            g.command('missing-command', 'Command not found: /bin/missing-command')
            g.type('execute'); g.key('ret'); g.expect('Barnix Execute')
            # A user's own home is browsable; listing protected /home is denied.
            g.key('h'); g.expect('/home/tester')
            g.key('backspace'); g.expect('Barnix Execute - /home')
            g.expect('Cannot read directory: access denied')
            g.key('esc'); g.expect('bssh>')
        finally:
            g.close()
        assert 'Type: directory' in debug(disk, 'stat /work/alpha')
        assert 'Inode:' in debug(disk, 'stat /work/b.txt')
        assert 'Inode:' not in debug(disk, 'stat /work/c.txt')
        subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    print('Execute file operations, USB mouse, permissions, ABI 7/8 and native/Linux launch passed')


if __name__ == '__main__':
    main()
