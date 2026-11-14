"""Put the Intel Wi-Fi firmware into a Barnix ext2 image (/lib/firmware).

wifi_iwl.c drives the card of the user's laptop (iwlwifi "So" family, Intel
AX201 CNVi). Intel's firmware may be redistributed unmodified with its licence
(linux-firmware, LICENCE.iwlwifi_firmware); it is taken from the host.
BARNIX_NO_FIRMWARE=1 leaves it out.
"""
import os
import pathlib
import subprocess
import tempfile

FIRMWARE = ['iwlwifi-so-a0-hr-b0-89.ucode']
SEARCH = [pathlib.Path('/lib/firmware/intel/iwlwifi'), pathlib.Path('/lib/firmware')]
LICENCE = pathlib.Path('/usr/share/licenses/linux-firmware-intel/LICENCE.iwlwifi_firmware')


def install(image):
    if os.environ.get('BARNIX_NO_FIRMWARE'):
        return False

    def debug(command):
        return subprocess.run(['debugfs', '-w', '-R', command, str(image)], check=True, capture_output=True, text=True)
    done = []
    with tempfile.TemporaryDirectory(prefix='barnix-firmware-') as temporary:
        for name in FIRMWARE:
            source = next((d / name for d in SEARCH if (d / name).is_file()), None)
            packed = next((d / (name + '.zst') for d in SEARCH if (d / (name + '.zst')).is_file()), None)
            if not source and packed:
                source = pathlib.Path(temporary) / name
                with source.open('wb') as out:
                    subprocess.run(['zstd', '-dc', str(packed)], check=True, stdout=out)
            if not source:
                continue
            for directory in ('/lib', '/lib/firmware'):
                if 'Inode:' not in debug(f'stat {directory}').stdout:
                    debug(f'mkdir {directory}')
            for src, dst in ((source, name), (LICENCE, 'LICENCE.iwlwifi_firmware')):
                if not src.is_file():
                    continue
                debug(f'rm /lib/firmware/{dst}')
                if 'Allocated inode:' not in debug(f'write "{src}" /lib/firmware/{dst}').stdout:
                    raise RuntimeError(f'cannot write /lib/firmware/{dst}')
            done.append(name)
    print('Wi-Fi firmware:', ', '.join(done) if done else 'not found on this computer')
    return bool(done)


if __name__ == '__main__':
    import sys
    install(pathlib.Path(sys.argv[1]))
