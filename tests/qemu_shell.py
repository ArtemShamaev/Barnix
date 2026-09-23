"""Shared QEMU VGA/keyboard runner for shell integration tests."""
import subprocess
import time


def run(disk, directory, commands, live_only=False, command_timeout=10):
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
        time.sleep(3)
        if live_only:
            monitor('sendkey down')
            time.sleep(0.2)
        monitor('sendkey ret')
        time.sleep(3)
        text = screen()
        assert 'bssh>' in text, ('Shell did not start', text)
        for command, expected in commands:
            for character in command:
                key = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus',
                       '|': 'shift-backslash', '<': 'shift-comma', '>': 'shift-dot',
                       "'": 'apostrophe', '"': 'shift-apostrophe', '\\': 'backslash',
                       ';': 'semicolon', '&': 'shift-7'}.get(character, character)
                monitor(f'sendkey {key} 1')
                time.sleep(0.025)
            monitor('sendkey ret 1')
            # ATA flushes and software-emulated Linux tasks can take longer
            # than a fixed one-second delay. Wait for this command's prompt.
            deadline = time.monotonic() + command_timeout
            while True:
                text = screen()
                marker = 'bssh> ' + command
                start = text.rfind(marker)
                current = text[start:] if start >= 0 else text
                if expected in current and text.rstrip().endswith('bssh>'):
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError((command, expected, text))
                time.sleep(0.1)
        print('Shell sequence passed')
        monitor('quit')
        process.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
