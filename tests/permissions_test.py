import pathlib
import subprocess
import tempfile
with tempfile.TemporaryDirectory(prefix='barnix-permissions-') as temporary:
    image=pathlib.Path(temporary)/'test.img'
    image.write_bytes(bytes(2*1024*1024))
    subprocess.run(['mke2fs','-q','-t','ext2','-F','-b','1024','-I','128','-N','128','-O','none,filetype',str(image)],check=True)
    subprocess.run(['stdbuf','-o0','./tests/permissions_test',str(image)],check=True)
