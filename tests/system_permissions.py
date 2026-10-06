"""Boot real installed BIOS/UEFI systems and exercise auth, games and partitioning."""
from pathlib import Path
import sys, tempfile, struct, time, subprocess
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Setup'))
from test_setup import Guest,ROOT,debug

def type_text(g,text):
    mapping={' ':'spc','/':'slash','.':'dot','-':'minus','=':'equal','_':'shift-minus','|':'shift-backslash','>':'shift-dot',':':'shift-semicolon'}
    for c in text:g.key(mapping.get(c, 'shift-'+c.lower() if c.isupper() else c))
Guest.type=type_text

def login(g,user='alice',password='alicepw'):
    g.expect('Login:',60);g.type(user);g.key('ret');g.expect('Password:');g.type(password);g.key('ret');g.expect('bssh>',60)

def users(g):
    g.expect('Create your username');g.type('alice');g.key('ret')
    for title,secret in [('Your password','alicepw'),('Repeat password','alicepw'),('Separate root password','rootpw'),('Repeat password','rootpw')]:
        g.expect(title);g.type(secret);g.key('ret')
    g.expect('Review and confirm')

def su(g,user,password):
    g.type('su '+user);g.key('ret');time.sleep(.2)
    if password:g.expect('Password:');g.type(password);g.key('ret')
    g.expect('bssh>');g.command('whoami',user)

def install(directory,target,mode,efi=False):
    g=Guest(directory,[target],efi=efi)
    try:
        g.expect('Welcome to Barnix Setup',60);g.key('ret');g.expect('Where should');g.key('ret');g.expect('Partitioning mode')
        for _ in range(mode):g.key('down')
        g.key('ret')
        if mode==2:
            g.expect('Manual partition table')
            for command in ['n 3 4 8','r 3','w']:
                g.type(command);g.key('ret');time.sleep(.2)
        g.expect('Choose what to install');g.key('down');g.key('ret');g.expect('Make this system yours');g.key('ret');users(g)
        g.type('disk1');g.key('ret');g.expect('Barnix is installed!',180)
    finally:g.close()

def exercise(g,directory):
    login(g);g.command('whoami','alice');g.command('pwd','/home/alice')
    g.command('write test hello','program exited: 0')
    g.command('write /etc/userpasswd.cfg evil','program exited: 1')
    g.command('write /home/root/evil x','program exited: 1')
    g.command('echo pipeline | cat','pipeline')
    g.command('echo redirection > output','program exited: 0');g.command('cat output','redirection')
    g.command('theme bios','Theme applied and saved')
    g.screen();cells=(directory/'screen.bin').read_bytes();assert any(a>>4==1 for a in cells[1::2]),'theme did not change background'
    g.type('snake');g.key('ret');g.expect('Snake  Arrows')
    first=g.screen();time.sleep(.25);second=g.screen();assert first!=second,'snake does not move without input'
    g.key('down');time.sleep(.12);g.key('p');time.sleep(.1);first=g.screen();time.sleep(.2);assert g.screen()==first,'snake pause failed'
    g.key('q');g.expect('bssh>');g.command('whoami','alice')
    g.type('pong');g.key('ret');g.expect('Pong vs computer');first=g.screen();time.sleep(.2);assert g.screen()!=first,'pong ball does not move';g.key('q');g.expect('bssh>')
    g.type('sudo whoami');g.key('ret');g.expect('[sudo] root password:');g.type('wrong');g.key('ret');g.expect('Authentication failed');g.command('whoami','alice')
    g.type('sudo whoami');g.key('ret');g.expect('[sudo] root password:');g.type('rootpw');g.key('ret');time.sleep(.5);text=g.screen();assert '\nroot\n' in text,text;g.command('whoami','alice')
    su(g,'root','rootpw');g.command('chmod alice=r passwd=unlock file=/home/alice/test','program exited: 0')
    g.command('chmod alice=x file=/bin/cat','program exited: 0')
    su(g,'alice','');g.command('write test blocked','program exited: 1')
    g.command('chmod alice=rw passwd=wrong file=test','program exited: 1')
    g.command('chmod alice=rw passwd=unlock file=test','program exited: 0')
    g.command('write test persisted','program exited: 0');g.command('cat test','persisted')
    g.command('cat /etc/userpasswd.cfg','program exited: 1')
    g.command('chmod alice=rw passwd=unlock file=/etc/userpasswd.cfg','program exited: 1')
    g.command('theme green','Theme applied and saved')

def main():
    cases=[(0,False),(1,False),(2,False),(1,True)]
    if "--remaining" in sys.argv:cases=cases[2:]
    for mode,efi in cases:
        with tempfile.TemporaryDirectory(prefix='barnix-system-') as tmp:
            directory=Path(tmp);target=directory/'disk.img';size=(192 if efi else 24)*1024*1024
            with target.open('wb') as f:f.truncate(size)
            sentinel=b'EXISTING-PARTITION'+bytes(4096-18)
            if mode==1:
                mbr=bytearray(512);mbr[510:]=b'\x55\xaa';struct.pack_into('<B3sB3sII',mbr,446,0,b'\0'*3,0x83,b'\0'*3,2048,8192)
                with target.open('r+b') as f:f.write(mbr);f.seek(2048*512);f.write(sentinel)
            install(directory,target,mode,efi)
            with target.open('rb') as f:
                mbr=f.read(512)
                if mode==1:f.seek(2048*512);assert f.read(len(sentinel))==sentinel
            root_slot=2 if mode==2 or efi else 1 if mode==1 else 0
            start,length=struct.unpack_from('<II',mbr,454+16*root_slot)
            fs=directory/'root.ext2'
            with target.open('rb') as f:f.seek(start*512);fs.write_bytes(f.read(length*512))
            subprocess.run(['e2fsck','-fn',str(fs)],check=True)
            records=debug(fs,'cat /etc/userpasswd.cfg');assert '$bx1$' in records and 'alicepw' not in records
            g=Guest(directory,[target],setup=False,efi=efi)
            try:
                if mode==0:exercise(g,directory)
                else:login(g);g.command('whoami','alice');g.command('pwd','/home/alice')
            finally:g.close()
            if mode==0:
                g=Guest(directory,[target],setup=False)
                try:login(g);g.command('cat test','persisted');g.command('cat useretc/theme.cfg','green')
                finally:g.close()
            print(f'Installed boot + checks passed: mode={mode}, efi={efi}',flush=True)
if __name__=='__main__':main()
