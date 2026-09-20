"""Smoke-test the built ISO using a temporary disk and QEMU's VGA memory."""
import pathlib
import subprocess
import tempfile
import time


def run(disk, directory, commands, live_only=False):
    args = ['qemu-system-i386', '-m', '256M', '-boot', 'd', '-cdrom', 'barnix.iso',
            '-display', 'none', '-serial', 'none', '-monitor', 'stdio', '-no-reboot']
    if disk is not None:
        args += ['-drive', f'file={disk},format=raw,if=ide']
    process = subprocess.Popen(
        args,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def monitor(command):
        process.stdin.write((command + '\n').encode())
        process.stdin.flush()

    def screen():
        destination = directory / 'vga.bin'
        monitor(f'pmemsave 0xb8000 4000 "{destination}"')
        time.sleep(0.2)
        data = destination.read_bytes()[::2]
        return '\n'.join(data[i:i + 80].decode('ascii', errors='replace').rstrip()
                         for i in range(0, 2000, 80))

    try:
        time.sleep(2)
        if live_only:
            monitor('sendkey down')
            time.sleep(0.2)
        monitor('sendkey ret')
        time.sleep(2)
        assert 'bssh>' in screen(), 'Shell did not start'
        for command, expected in commands:
            for character in command:
                key = {' ': 'spc', '.': 'dot', '/': 'slash'}.get(character, character)
                monitor(f'sendkey {key} 1')
                time.sleep(0.025)
            monitor('sendkey ret 1')
            time.sleep(0.25)
            text = screen()
            assert expected in text, (command, expected, text)
        print('Shell sequence passed')
        monitor('quit')
        process.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


with tempfile.TemporaryDirectory(prefix='barnix-shell-') as temporary:
    directory = pathlib.Path(temporary)
    disk = directory / 'test.img'
    with disk.open('wb') as image:
        image.truncate(2 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024',
                    '-I', '128', '-N', '64', '-O', 'none,filetype', str(disk)], check=True)
    run(disk, directory, [
        ('write a hello', 'bssh>'),
        ('append a world', 'bssh>'),
        ('cp a b', 'bssh>'),
        ('mv b c', 'bssh>'),
        ('cat c', 'helloworld'),
        ('stat c', 'Bytes: 10'),
        ('mkdir docs', 'bssh>'),
        ('cd docs', 'bssh>'),
        ('pwd', '/docs'),
        ('cd ..', 'bssh>'),
        ('rmdir docs', 'bssh>'),
        ('df', 'Free inodes: 51 / 64'),
        ('diskinfo', 'Device sectors (512 bytes): 4096'),
        ('sync', 'filesystem saved'),
    ])
    run(disk, directory, [('cat c', 'helloworld'), ('df', 'Free inodes: 51 / 64')])
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    result = subprocess.run(['debugfs', '-R', 'cat /c', str(disk)], capture_output=True, text=True, check=True)
    assert result.stdout == 'helloworld'
    print('QEMU shell, persistence and ext2 interoperability tests passed')

    run(None, directory, [
        ('diskinfo', 'Device: RAM disk (volatile)'),
        ('write optical hello', 'bssh>'),
        ('cat optical', 'hello'),
        ('mkdir docs', 'bssh>'),
        ('cd docs', 'bssh>'),
        ('pwd', '/docs'),
        ('sync', 'filesystem saved'),
    ])
    run(None, directory, [('stat optical', 'operation failed'),
                          ('df', 'Free inodes: 53 / 64')])
    before = disk.read_bytes()
    run(disk, directory, [('diskinfo', 'Device: RAM disk (volatile)'),
                          ('write live temporary', 'bssh>'),
                          ('cat live', 'temporary')], live_only=True)
    assert disk.read_bytes() == before, 'Live-only mode modified the ATA disk'
    # An unrecognized attached disk must not be formatted by the fallback.
    disk.write_bytes(bytes(2 * 1024 * 1024))
    run(disk, directory, [('diskinfo', 'Device: RAM disk (volatile)'),
                          ('write fallback ram', 'bssh>')])
    assert disk.read_bytes() == bytes(2 * 1024 * 1024)
    print('CD/DVD boot, RAM writes, volatile restart, and ATA isolation tests passed')
