#!/usr/bin/env python3
"""Build separate Legacy BIOS and x64 UEFI installation LiveCDs (no loop devices)."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SETUP = ROOT / 'Setup'
BUILD = SETUP / 'build'
PARTITION_LBA = 2048
FS_SECTORS = 4096
IMAGE_SIZE = (PARTITION_LBA + FS_SECTORS) * 512
sys.path.insert(0, str(ROOT / 'scripts'))
from install_apps import install
from add_elf import complete_apps


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def debug(image, command):
    result = subprocess.run(['debugfs', '-w', '-R', command, str(image)],
                            check=True, capture_output=True, text=True)
    if any(message in result.stderr for message in ('File not found', 'Could not allocate', 'No space')):
        raise RuntimeError(result.stderr)
    return result.stdout


def boot_prefix(directory):
    config = directory / 'early.cfg'
    config.write_text('set timeout=0\n')
    cores = []
    for slot in range(4):
        core_path = directory / f'core{slot}.img'
        run('grub-mkimage', '-O', 'i386-pc', '-o', core_path,
            '-p', f'(,msdos{slot + 1})/boot/grub', '-c', config,
            'biosdisk', 'part_msdos', 'ext2', 'multiboot', 'probe', 'normal')
        core = bytearray(core_path.read_bytes())
        sectors = (len(core) + 511) // 512
        if len(core) < 512 or sectors >= 511:
            raise ValueError('GRUB core does not fit its embedding slot')
        struct.pack_into('<QHH', core, 500, 2 + slot * 512, sectors - 1, 0x820)
        core[488:500] = bytes(12)
        cores.append(core)
    boot = bytearray(Path('/usr/lib/grub/i386-pc/boot.img').read_bytes())
    if len(boot) != 512 or boot[510:] != b'\x55\xaa':
        raise ValueError('Unsupported GRUB boot.img')
    struct.pack_into('<Q', boot, 0x5c, 1)
    boot[0x64] = 0xff  # Use the BIOS-provided drive, not a hardcoded hd0.
    boot[446:510] = bytes(64)
    struct.pack_into('<B3sB3sII', boot, 446, 0x80, b'\xfe\xff\xff', 0x83,
                     b'\xfe\xff\xff', PARTITION_LBA, FS_SECTORS)
    prefix = bytearray(PARTITION_LBA * 512)
    prefix[:512] = boot
    for slot, core in enumerate(cores):
        start = (1 + slot * 512) * 512
        prefix[start:start + len(core)] = core
    return prefix


def efi_partition(directory):
    """FAT32 ESP with the standard removable-media fallback boot path."""
    config = directory / 'efi-early.cfg'
    config.write_text("regexp --set=1:bootdisk '^\\(([^,]+),' \"$cmdpath\"\n"
                      'set root=($bootdisk,msdos2)\n'
                      'set prefix=($bootdisk,msdos2)/boot/grub\n'
                      'configfile $prefix/grub.cfg\n')
    executable = directory / 'BOOTX64.EFI'
    run('grub-mkimage', '-O', 'x86_64-efi', '--compression=none', '-o', executable,
        '-p', '/EFI/BOOT', '-c', config, 'part_msdos', 'fat', 'ext2',
        'normal', 'configfile', 'regexp', 'multiboot', 'probe', 'efi_gop',
        'video', 'video_bochs', 'video_cirrus', 'echo', 'reboot')
    if b'msdos2' not in executable.read_bytes():
        raise ValueError('EFI image must expose the partition slot')
    shutil.copyfile(executable, BUILD / 'BOOTX64.EFI')
    image = directory / 'esp.img'
    with image.open('wb') as stream:
        stream.truncate(64 * 1024 * 1024)
    run('mkfs.fat', '-F', '32', '-h', '2048', '-n', 'BARNIX_EFI', image)
    run('mmd', '-i', image, '::/EFI', '::/EFI/BOOT')
    run('mcopy', '-i', image, executable, '::/EFI/BOOT/BOOTX64.EFI')
    run('fsck.fat', '-n', image)
    # Free clusters need not be copied. Keep the BPB, both FATs, directory and
    # every allocated file byte, including zero padding inside this prefix.
    data = image.read_bytes()
    reserved = struct.unpack_from('<H', data, 14)[0]
    fat_sectors = struct.unpack_from('<I', data, 36)[0]
    data_sector = reserved + data[16] * fat_sectors
    last_cluster = max(i for i in range(2, fat_sectors * 128)
                       if struct.unpack_from('<I', data, reserved * 512 + i * 4)[0] & 0x0fffffff)
    end = (data_sector + (last_cluster - 1) * data[13]) * 512
    if end > len(data):
        raise ValueError('Invalid FAT32 allocation map')
    (BUILD / 'efi-esp.bin').write_bytes(data[:end])


def main():
    BUILD.mkdir(exist_ok=True)
    manifest = json.loads((SETUP / 'profiles.json').read_text())
    profiles = manifest['profiles']
    if len(profiles) != 3:
        raise ValueError('The wizard currently requires three profiles')
    import re
    selected = {}
    for profile in profiles:
        if not re.fullmatch('[a-z][a-z0-9_-]{0,22}', profile['id']):
            raise ValueError('Invalid profile id')
        inherited = selected[profile['extends']][:] if 'extends' in profile else []
        for name in profile.get('commands', []):
            if not re.fullmatch('[a-z][a-z0-9_-]{0,22}', name):
                raise ValueError('Invalid command name')
            inherited.append(ROOT / 'apps/bin' / name)
        for name in profile.get('examples', []):
            if not re.fullmatch('[a-z][a-z0-9_.-]{0,22}', name):
                raise ValueError('Invalid example name')
            inherited.append(ROOT / 'apps' / name)
        if profile['id'] == 'complete':
            inherited.extend(complete_apps(ROOT))
        selected[profile['id']] = list(dict.fromkeys(inherited))
    with tempfile.TemporaryDirectory(prefix='images-', dir=BUILD) as temporary:
        temp = Path(temporary)
        prefix = boot_prefix(temp)
        efi_partition(temp)
        for mode in ('legacy', 'efi'):
            for profile in profiles:
                fs = temp / (mode + '-' + profile['id'] + '.ext2')
                with fs.open('wb') as stream:
                    stream.truncate(FS_SECTORS * 512)
                run('mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '256', '-O', 'none,filetype', fs)
                install(fs, selected[profile['id']])
                debug(fs, 'mkdir /boot')
                debug(fs, 'mkdir /boot/grub')
                grub_config = temp / 'grub.cfg'
                grub_config.write_text(('insmod efi_gop\nset gfxpayload=800x600x32,auto\n' if mode == 'efi' else '') +
                                       'set timeout=0\nset default=0\nmenuentry "Barnix" {\n'
                                       '  probe --set=boot_uuid --fs-uuid ($root)\n'
                                       '  multiboot ($root)/boot/kernel.elf root_uuid=$boot_uuid\n'
                                       '  boot\n}\n')
                debug(fs, f'write "{grub_config}" /boot/grub/grub.cfg')
                kernel = ROOT / ('kernel-efi.elf' if mode == 'efi' else 'kernel.elf')
                debug(fs, f'write "{kernel}" /boot/kernel.elf')
                for name, content in [('hostname', 'barnix\n'), ('install.cfg',
                                      'PROFILE=' + profile['id'] + '\n')]:
                    source = temp / name
                    source.write_text(content)
                    debug(fs, f'write "{source}" /etc/{name}')
                run('e2fsck', '-fn', fs)
                image = temp / (mode + '-' + profile['id'] + '.img')
                image.write_bytes(prefix + fs.read_bytes())
                image.replace(BUILD / image.name)
            staging = temp / ('iso-' + mode)
            boot = staging / 'boot'
            (boot / 'grub').mkdir(parents=True)
            shutil.copyfile(SETUP / ('kernel-efi.elf' if mode == 'efi' else 'kernel.elf'), boot / 'kernel.elf')
            shutil.copyfile(ROOT / 'live-ext2.img', boot / 'live-ext2.img')
            for profile in profiles:
                shutil.copyfile(BUILD / (mode + '-' + profile['id'] + '.img'), boot / (profile['id'] + '.img'))
            modules = ''.join(f'    module /boot/{p["id"]}.img {p["id"]}\n' for p in profiles)
            if mode == 'efi':
                shutil.copyfile(BUILD / 'efi-esp.bin', boot / 'efi-esp.bin')
                modules += '    module /boot/efi-esp.bin esp\n'
            (boot / 'grub/grub.cfg').write_text(
                'set timeout=3\nset default=0\n' +
                ('insmod efi_gop\nset gfxpayload=800x600x32,auto\n' if mode == 'efi' else '') +
                f'menuentry "Barnix {mode.upper()} Setup" {{\n'
                '    multiboot /boot/kernel.elf live\n'
                '    module /boot/live-ext2.img\n' + modules + '    boot\n}\n')
            name = f'barnix-{mode}-setup.iso'
            output = BUILD / name
            platform = 'x86_64-efi' if mode == 'efi' else 'i386-pc'
            run('grub-mkrescue', '-d', '/usr/lib/grub/' + platform, '-o', output, staging)
            output.replace(ROOT / name)
            print('Built ' + name)
    obsolete = SETUP / 'barnix-setup.iso'
    if obsolete.exists():
        obsolete.unlink()


def header():
    BUILD.mkdir(exist_ok=True)
    data = json.loads((SETUP / 'profiles.json').read_text())
    if len(data['profiles']) != 3:
        raise ValueError('The wizard currently requires three profiles')
    lines = ['/* Generated from Setup/profiles.json. */',
             '#define SETUP_DISTRIBUTION ' + json.dumps(data['distribution']),
             'static const char *profile_ids[] = {' + ','.join(json.dumps(p['id']) for p in data['profiles']) + '};',
             'static const char *profile_titles[] = {' + ','.join(json.dumps(p['title']) for p in data['profiles']) + '};',
             'static const char *profile_descriptions[] = {' + ','.join(json.dumps(p['description']) for p in data['profiles']) + '};']
    (BUILD / 'profiles.h').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    if sys.argv[1:] == ['--header']:
        header()
    else:
        main()
