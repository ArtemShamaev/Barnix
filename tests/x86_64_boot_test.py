"""x86_64 stage 0: barnix64.iso reaches long mode under Legacy BIOS and UEFI."""
import pathlib
import subprocess
import sys
import tempfile
import time

OVMF = pathlib.Path('/usr/share/edk2/x64/OVMF_CODE.4m.fd')
OVMF_VARS = pathlib.Path('/usr/share/edk2/x64/OVMF_VARS.4m.fd')


def iso_with(directory, command_line):
    """barnix64.iso's tree with extra kernel arguments, as a new ISO."""
    tree = directory / 'iso'
    if tree.exists():
        subprocess.run(['rm', '-rf', str(tree)], check=True)
    subprocess.run(['cp', '-r', 'build/iso64', str(tree)], check=True)
    (tree / 'boot/grub/grub.cfg').write_text(
        f'set timeout=0\nmenuentry "Barnix x86_64" {{\n    multiboot /boot/kernel64.elf {command_line}\n    boot\n}}\n')
    image = directory / 'test.iso'
    subprocess.run(['grub-mkrescue', '-o', str(image), str(tree), '--', '-volid', 'BARNIX64'],
                   check=True, capture_output=True)
    return image


def boot(firmware, directory, iso='barnix64.iso', marker='stage 0 done'):
    log = directory / f'serial-{firmware}.log'
    if log.exists():
        log.unlink()
    args = ['qemu-system-x86_64', '-m', '6G', '-cdrom', str(iso), '-display', 'none',
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
            if marker in screen or 'bad Multiboot' in screen or 'does not support' in screen:
                break
    finally:
        process.stdin.write(b'quit\n')
        process.stdin.flush()
        process.wait(timeout=10)
    assert 'long mode OK' in screen and 'pointer size 8' in screen and 'stage 0 done' in screen, screen
    if iso == 'barnix64.iso':
        usable = [line for line in screen.splitlines() if line.startswith('usable RAM')]
        print(f'{firmware}: {usable[0] if usable else "no memory map"}')
    return screen


with tempfile.TemporaryDirectory(prefix='barnix64-') as temporary:
    directory = pathlib.Path(temporary)
    modes = ['legacy'] + (['efi'] if OVMF.is_file() and OVMF_VARS.is_file() else [])
    for mode in modes:
        screen = boot(mode, directory)
    # CPU exceptions: a report with the cause, never a silent reset.
    for fault, cause in [('div', '#DE divide error'), ('gp', '#GP general protection'),
                         ('pf', '#PF page fault'), ('stack', '#DF double fault')]:
        image = iso_with(directory, f'fault={fault}')
        for mode in modes:
            screen = boot(mode, directory, image, 'System halted.')
            assert 'KERNEL PANIC' in screen and cause in screen, (fault, mode, screen)
        print(f'fault={fault}: {cause} reported ({", ".join(modes)})')
    if 'efi' not in modes:
        print('OVMF not found: UEFI boot skipped', file=sys.stderr)
print('x86_64 long-mode boot passed')
