"""All installer profiles ship Execute; fresh EFI disk boots its desktop."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Setup'))
from test_setup import Guest, debug


def main():
    with tempfile.TemporaryDirectory(prefix='execute-efi-') as temporary:
        work=Path(temporary)
        for firmware in ('legacy','efi'):
            for profile in ('minimal','standard','complete'):
                image=ROOT/f'Setup/build/{firmware}-{profile}.img'
                template=image.read_bytes()
                start=struct.unpack_from('<I',template,454)[0]*512
                image=work/f'{firmware}-{profile}.ext2'
                image.write_bytes(template[start:])
                assert 'Mode:  0755' in debug(image,'stat /bin/execute'), (firmware,profile)
                extracted=work/f'{firmware}-{profile}-execute'
                debug(image,f'dump /bin/execute {extracted}')
                assert extracted.read_bytes()==(ROOT/'apps/bin/execute').read_bytes()
        target=work/'target.img'
        with target.open('wb') as f:f.truncate(96*1024*1024)
        guest=Guest(work,[target],efi=True)
        try:
            guest.expect('Welcome to Barnix Setup',60);guest.key('ret')
            guest.expect('Where should');guest.key('ret');guest.expect('Partitioning mode');guest.key('ret')
            guest.expect('Choose what to install');guest.key('ret') # default Standard
            guest.expect('Make this system yours');guest.key('ret');guest.accounts()
            guest.expect('Review and confirm');guest.type('disk1');guest.key('ret')
            guest.expect('Barnix is installed!',180)
        finally:guest.close()
        guest=Guest(work,[target],setup=False,efi=True)
        try:
            guest.expect('Login:',60);guest.type('alice');guest.key('ret')
            guest.expect('Password:');guest.type('alicepw');guest.key('ret');guest.expect('bssh>')
            guest.type('execute');guest.key('ret')
            symbols=subprocess.check_output(['nm',str(ROOT/'apps/bin/execute')],text=True)
            title=next(int(row.split()[0],16) for row in symbols.splitlines() if row.endswith(' title'))
            data=work/'title.bin'
            for _ in range(60):
                guest.monitor(f'pmemsave {title:#x} 320 "{data}"');time.sleep(.15)
                if data.exists() and data.read_bytes().startswith(b'Barnix Execute - /home/alice'):break
            else:raise AssertionError(('Execute did not start',guest.screen()))
            guest.screenshot('execute-efi.ppm')
            shutil.copyfile(work/'execute-efi.ppm',ROOT/'Setup/build/execute-efi.ppm')
            guest.key('esc');guest.expect('bssh>')
            guest.command('echo execute-efi-ok','execute-efi-ok')
        finally:guest.close()
    print('Execute present in all six profiles; fresh EFI installation and regular-user desktop launch passed')


if __name__=='__main__':main()
