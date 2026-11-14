"""x86_64 stage 0: barnix64.iso reaches long mode under Legacy BIOS and UEFI."""
import pathlib
import subprocess
import sys
import tempfile
import time

OVMF = pathlib.Path('/usr/share/edk2/x64/OVMF_CODE.4m.fd')
OVMF_VARS = pathlib.Path('/usr/share/edk2/x64/OVMF_VARS.4m.fd')


def boot(firmware, directory):
    log = directory / f'serial-{firmware}.log'
    args = ['qemu-system-x86_64', '-m', '6G', '-cdrom', 'barnix64.iso', '-display', 'none',
            '-serial', f'file:{log}', '-monitor', 'stdio', '-no-reboot']
    if firmware == 'efi':
        variables = directory / 'vars.fd'
        variables.write_bytes(OVMF_VARS.read_bytes())
        args += ['-drive', f'if=pflash,format=raw,readonly=on,file={OVMF}',
                 '-drive', f'if=pflash,format=raw,file={variables}']
    process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    screen = ''
    try:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            time.sleep(0.5)
            screen = log.read_text('ascii', 'replace') if log.exists() else ''
            if 'stage 0 done' in screen or 'bad Multiboot' in screen or 'does not support' in screen:
                break
    finally:
        process.stdin.write(b'quit\n')
        process.stdin.flush()
        process.wait(timeout=10)
    assert 'long mode OK' in screen and 'pointer size 8' in screen and 'stage 0 done' in screen, screen
    usable = [line for line in screen.splitlines() if line.startswith('usable RAM')]
    print(f'{firmware}: {usable[0] if usable else "no memory map"}')
    return screen


with tempfile.TemporaryDirectory(prefix='barnix64-') as temporary:
    directory = pathlib.Path(temporary)
    modes = ['legacy'] + (['efi'] if OVMF.is_file() and OVMF_VARS.is_file() else [])
    for mode in modes:
        screen = boot(mode, directory)
    if 'efi' not in modes:
        print('OVMF not found: UEFI boot skipped', file=sys.stderr)
print('x86_64 long-mode boot passed')
