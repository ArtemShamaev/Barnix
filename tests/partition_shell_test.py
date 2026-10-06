"""Guest fdisk writes only a temporary secondary disk and respects mounts."""
import pathlib,shutil,subprocess,tempfile,struct
from qemu_shell import run
with tempfile.TemporaryDirectory(prefix='barnix-fdisk-') as temp:
    directory=pathlib.Path(temp);root=directory/'root.img';disk=directory/'data.img'
    shutil.copyfile('live-ext2.img',root);disk.write_bytes(bytes(16*1024*1024))
    run(root,directory,[
        ('fdisk disk2 list','SLOT START_LBA'),
        ('fdisk disk2 new 1 1 4','program exited: 0'),
        ('fdisk disk2 new 2 5 4','program exited: 0'),
        ('fdisk disk2 new 3 2 4','program exited: 1'),
        ('fdisk disk2 list','2 10240 8192 131'),
        ('format disk2p1','formatted: empty ext2'),
        ('format disk2p2','formatted: empty ext2'),
        ('mkdir /one','program exited: 0'),('mkdir /two','program exited: 0'),
        ('mount disk2p1 /one','on /one'),('mount disk2p2 /two','on /two'),
        ('write /one/note first','program exited: 0'),('write /two/note second','program exited: 0'),
        ('cat /one/note','first'),('cat /two/note','second'),
        ('fdisk disk2 delete 1','Unmount all partitions'),
        ('format disk2p1','format failed'),
        ('unmount /one','filesystem unmounted'),('unmount /two','filesystem unmounted'),
        ('fdisk disk2 delete 2','program exited: 0'),
        ('fdisk disk2 auto 2','program exited: 0'),
        ('unmount /','filesystem unmounted'),
        ('fdisk disk2 list','1 2048 8192 131'),
    ],extra_disks=[disk],command_timeout=30)
    data=disk.read_bytes();assert struct.unpack_from('<II',data,454)==(2048,8192)
    assert struct.unpack_from('<II',data,470)==(10240,4096)
    part=directory/'part.ext2';part.write_bytes(data[1024*1024:5*1024*1024]);subprocess.run(['e2fsck','-fn',str(part)],check=True)
print('fdisk, partition devices, simultaneous mounts, root recovery and isolation passed')
