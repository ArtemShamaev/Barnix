"""PS/2 clicks through the installer, BMP painting/undo/load on BIOS and GOP."""
from pathlib import Path
import sys,tempfile,subprocess,time,struct,os
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Setup'))
from test_setup import Guest,ROOT,debug
sys.path.insert(0,str(Path(__file__).resolve().parent))
from system_permissions import login

def symbols(g,setup):
    kernel=ROOT/('Setup/' if setup else '')/('kernel-efi.elf' if g.efi else 'kernel.elf')
    rows=subprocess.check_output(['nm',str(kernel)],text=True).splitlines()
    return next(int(row.split()[0],16) for row in rows if row.endswith(' ps2_mouse_state'))

def state(g,address):
    path=g.directory/'mouse.bin';g.monitor(f'pmemsave {address:#x} 20 "{path}"');time.sleep(.06)
    return struct.unpack('<iiIIi',path.read_bytes())

def move(g,address,x,y):
    for _ in range(15):
        current=state(g,address);dx=max(-100,min(100,x-current[0]));dy=max(-100,min(100,y-current[1]))
        if not dx and not dy:return
        g.monitor(f'mouse_move {dx} {dy}');time.sleep(.07)
    raise AssertionError(('mouse did not reach target',x,y,state(g,address)))

def click(g,address,x,y):
    move(g,address,x,y);g.monitor('mouse_button 1');time.sleep(.12);g.monitor('mouse_button 0');time.sleep(.15)

def extract(image,directory,efi):
    data=image.read_bytes();slot=1 if efi else 0;start,size=struct.unpack_from('<II',data,454+slot*16)
    path=directory/'root.ext2';path.write_bytes(data[start*512:(start+size)*512]);return path

usb_mouse = "--usb" in sys.argv
for efi in ((True,) if os.environ.get("USB_EFI_ONLY") else (False,True)):
    with tempfile.TemporaryDirectory(prefix='barnix-mouse-paint-') as tmp:
        directory=Path(tmp);target=directory/'disk.img'
        with target.open('wb') as f:f.truncate((96 if efi else 16)*1024*1024)
        g=Guest(directory,[target],efi=efi,usb_mouse=usb_mouse)
        try:
            g.expect('Welcome to Barnix Setup',60);address=symbols(g,True);assert state(g,address)[4]==1,'mouse unavailable'
            click(g,address,340,248);g.expect('Добро пожаловать')
            click(g,address,340,216);g.expect('Welcome to Barnix Setup')
            click(g,address,548,360);g.expect('Where should')
            click(g,address,548,360);g.expect('Partitioning mode')
            click(g,address,548,360);g.expect('Choose what to install')
            click(g,address,340,200);click(g,address,548,360);g.expect('Make this system yours')
            click(g,address,548,360);g.accounts();g.expect('Review and confirm')
            click(g,address,548,360);g.expect('Review and confirm')
            assert target.read_bytes()==bytes(target.stat().st_size),'mouse bypassed confirmation'
            g.type('disk1');click(g,address,548,360);g.expect('Barnix is installed!',180)
        finally:g.close()
        g=Guest(directory,[target],setup=False,efi=efi,usb_mouse=usb_mouse)
        try:
            login(g);address=symbols(g,False);assert state(g,address)[4]==1
            if usb_mouse:
                g.monitor('mouse_button 1');time.sleep(.2)
                assert state(g,address)[2]&1
                g.monitor('device_del usbmouse');time.sleep(.5)
                assert state(g,address)[2]==0,'unplug left button stuck'
                g.monitor('mouse_button 0');time.sleep(.2)
                g.monitor('device_add usb-mouse,bus=mouseusb.0,port=1,id=usbmouse');time.sleep(.6)
                move(g,address,120,120)
            g.type('paint');g.key('ret');g.expect('[ New ]');g.expect('/home/alice/picture.bmp')
            click(g,address,180,40) # palette red
            move(g,address,88,144);g.monitor('mouse_button 1');time.sleep(.1)
            move(g,address,160,192);g.monitor('mouse_button 0');time.sleep(.1)
            click(g,address,152,8);g.expect('Picture saved.')
            g.screenshot('paint.ppm');import shutil;shutil.copyfile(directory/'paint.ppm',ROOT/'Setup/build'/('paint-efi.ppm' if efi else 'paint-bios.ppm'))
            click(g,address,25,8);g.expect('New canvas.');click(g,address,210,8) # undo clear
            click(g,address,152,8);g.expect('Picture saved.')
            click(g,address,590,8);g.expect('bssh>')
            g.type('paint');g.key('ret');g.expect('Picture loaded.')
            move(g,address,88,144);g.monitor('mouse_button 2');time.sleep(.12);g.monitor('mouse_button 0');time.sleep(.12)
            click(g,address,210,8) # undo erase
            click(g,address,152,8);g.expect('Picture saved.');click(g,address,590,8);g.expect('bssh>')
            g.command('whoami','alice')
            # Saving through the same application must not bypass /etc protection.
            g.type('paint /etc/forbidden.bmp');g.key('ret');g.expect('[ New ]');click(g,address,152,8);g.expect('Save failed:');click(g,address,590,8);g.expect('bssh>')
        finally:g.close()
        fs=extract(target,directory,efi);bmp=directory/'picture.bmp'
        subprocess.run(['debugfs','-R',f'dump /home/alice/picture.bmp {bmp}',str(fs)],check=True,capture_output=True)
        data=bmp.read_bytes();assert data[:2]==b'BM' and struct.unpack_from('<ii',data,18)==(78,36)
        stride=(78*3+3)&~3
        red=sum(data[54+y*stride+x*3:57+y*stride+x*3]==bytes([0,0,170]) for y in range(36) for x in range(78))
        assert red>=8,('mouse stroke or undo lost',red)
        assert 'Inode:' not in debug(fs,'stat /etc/forbidden.bmp')
        subprocess.run(['e2fsck','-fn',str(fs)],check=True)
        print(f'Mouse installer, Paint BMP, undo, reload and permissions passed: efi={efi}',flush=True)
