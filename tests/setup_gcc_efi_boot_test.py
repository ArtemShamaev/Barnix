"""Boot the GCC-enabled EFI installer and invoke GCC from its Live shell."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Setup'))
from test_setup import Guest

with tempfile.TemporaryDirectory(prefix='barnix-setup-gcc-efi-') as temporary:
    directory = Path(temporary)
    target = directory / 'efi-target.img'
    with target.open('wb') as stream:
        stream.truncate(128 * 1024 * 1024)
    guest = Guest(directory, [target], efi=True, gcc=True)
    try:
        guest.expect('Welcome to Barnix Setup', 120)
        guest.key('esc'); guest.login_root()
        guest.command('gcc --version', '16.2.0')
    finally:
        guest.close()
print('EFI GCC Live shell passed')
