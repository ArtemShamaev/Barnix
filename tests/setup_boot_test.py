"""Boot both installer ISOs without a disk: root must come from the live module."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Setup'))
from test_setup import Guest


def main():
    for efi in (False, True):
        mode = 'UEFI' if efi else 'Legacy'
        with tempfile.TemporaryDirectory(prefix='barnix-setup-boot-') as temporary:
            guest = Guest(Path(temporary), [], efi=efi)
            try:
                guest.expect('Welcome to Barnix Setup', timeout=90)
                guest.key('esc')
                guest.login_root()
                guest.command('write livecheck writable', 'program exited: 0')
                guest.command('cat livecheck', 'writable')
                print(f'{mode}: installer boot, live filesystem mount and RAM writes passed', flush=True)
            finally:
                guest.close()


if __name__ == '__main__':
    main()
