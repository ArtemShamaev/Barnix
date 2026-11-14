"""Install the ISO-embedded GCC package offline, then run it from disk."""
from pathlib import Path
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Setup'))
from test_setup import Guest


def main():
    with tempfile.TemporaryDirectory(prefix='barnix-setup-gcc-') as temporary:
        directory = Path(temporary)
        target = directory / 'gcc-target.img'
        with target.open('wb') as stream:
            stream.truncate(512 * 1024 * 1024)
        guest = Guest(directory, [target], gcc=True)
        try:
            guest.expect('Welcome to Barnix Setup', 90)
            guest.key('esc'); guest.login_root()
            guest.command('gcc --version', '16.2.0')
            guest.command('g++ --version', '16.2.0')
        finally:
            guest.close()
        guest = Guest(directory, [target], gcc=True)
        try:
            guest.expect('Welcome to Barnix Setup', 90)
            guest.key('ret'); guest.expect('Where should Barnix be installed?')
            guest.key('ret'); guest.expect('Partitioning mode')
            guest.key('ret'); guest.expect('Choose what to install')
            guest.key('down'); guest.key('down'); guest.key('down')
            guest.key('ret'); guest.expect('Network settings')
            guest.key('ret'); guest.expect('Make this system yours')
            guest.key('ret'); guest.accounts(); guest.expect('Review and confirm')
            guest.type('virtio0'); guest.key('ret')
            guest.expect('Barnix is installed!', timeout=900)
        finally:
            guest.close()
        guest = Guest(directory, [target], setup=False, gcc=True)
        try:
            guest.login_root()
            guest.command('gcc --version', '16.2.0')
            guest.command('g++ --version', '16.2.0')
        finally:
            guest.close()
        print('ISO-embedded GCC installed offline; GCC/G++ ran from the installed disk')


if __name__ == '__main__':
    main()
