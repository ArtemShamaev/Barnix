"""Shared QEMU VGA/keyboard runner for shell integration tests."""
import subprocess
import time


def run(disk, directory, commands, live_only=False, command_timeout=10, extra_disks=(), extra_args=()):
    args = ['qemu-system-i386', '-m', '256M', '-boot', 'd', '-drive', 'file=barnix.iso,media=cdrom,if=ide,index=3',
            '-display', 'none', '-serial', 'none', '-monitor', 'stdio', '-no-reboot']
    if disk is not None:
        args += ['-drive', f'file={disk},format=raw,if=ide']
    for index, extra in enumerate(extra_disks, 1):
        args += ['-drive', f'file={extra},format=raw,if=ide,index={index}']
    args += list(extra_args)
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
        # GRUB boots the default entry after 1 s and Barnix can reach the shell
        # within seconds, so press keys only while the menu is on screen; a
        # blind Enter would become the first password.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            time.sleep(0.05)
            try:
                text = screen()
            except FileNotFoundError:
                continue
            if 'RAM only' in text:
                if live_only:
                    monitor('sendkey down')
                    time.sleep(0.1)
                monitor('sendkey ret')
                break
            if 'Booting `Barnix' in text or 'BARNIX' in text or 'starting shell' in text:
                assert not live_only, ('GRUB menu was missed', text)
                break
        time.sleep(3)
        text = screen()
        def send(text):
            for character in text:
                monitor('sendkey '+{' ':'spc','/':'slash','-':'minus'}.get(character,character)+' 1')
                time.sleep(.025)
            monitor('sendkey ret 1');time.sleep(.4)
        prompt_deadline = time.monotonic() + 30
        while not any(marker in text for marker in ('First start:', 'Login:', 'bssh>')) \
                and time.monotonic() < prompt_deadline:
            time.sleep(0.2)
            text = screen()
        if 'First start:' in text:
            for answer in ('rootpw','rootpw','tester','testerpw','testerpw'):
                send(answer)
            text=screen()
        if 'Login:' in text and 'bssh>' not in text:
            send('root');send('rootpw');text=screen()
        deadline = time.monotonic() + 15
        while 'bssh>' not in text and time.monotonic() < deadline:
            time.sleep(0.2)
            text = screen()
        assert 'bssh>' in text, ('Shell did not start', text)
        send('cd /');send('clear')
        for command, expected in commands:
            for character in command:
                key = {' ': 'spc', '.': 'dot', '/': 'slash', '-': 'minus',
                       '|': 'shift-backslash', '<': 'shift-comma', '>': 'shift-dot',
                       "'": 'apostrophe', '"': 'shift-apostrophe', '\\': 'backslash',
                       ';': 'semicolon', ':': 'shift-semicolon', '&': 'shift-7'}.get(character, character)
                if character.isupper():
                    key = 'shift-' + character.lower()
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
                # Long output (help) pauses after each page: continue it.
                if '-- more:' in text.rstrip().splitlines()[-1]:
                    monitor('sendkey spc 1')
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
