"""Install to an xHCI flash drive and boot it without the installer CD."""
from pathlib import Path
import struct
import subprocess
import tempfile
from test_setup import Guest, debug

for efi in (False, True):
    with tempfile.TemporaryDirectory(prefix='barnix-setup-usb-') as temporary:
        directory=Path(temporary)
        target=directory/'flash.img'
        with target.open('wb') as stream:
            stream.truncate((96 if efi else 16)*1024*1024)
        guest=Guest(directory,[target],transport='usb',efi=efi)
        try:
            guest.expect('Welcome to Barnix Setup',60)
            guest.key('ret');guest.expect('usb0')
            guest.key('ret');guest.expect('Partitioning mode')
            guest.key('ret');guest.expect('Choose what to install')
            guest.key('ret');guest.expect('Make this system yours')
            guest.key('ret');guest.accounts();guest.expect('Review and confirm')
            guest.type('usb0');guest.key('ret')
            guest.expect('Barnix is installed!',240)
        finally:
            guest.close()
        guest=Guest(directory,[target],setup=False,transport='usb',efi=efi)
        try:
            guest.login_root()
            guest.command('cat /etc/install.cfg','PROFILE=standard')
            guest.command('devices lsblk','usb0')
            guest.command('write persisted usbboot','program exited: 0')
        finally:
            guest.close()
        image=target.read_bytes()
        start,size=struct.unpack_from('<II',image,454+(16 if efi else 0))
        filesystem=directory/'root.ext2'
        filesystem.write_bytes(image[start*512:(start+size)*512])
        subprocess.run(['e2fsck','-fn',str(filesystem)],check=True)
        assert debug(filesystem,'cat /persisted')=='usbboot'
        print(f'xHCI installation, CD-free USB boot and persistence passed: efi={efi}',flush=True)
