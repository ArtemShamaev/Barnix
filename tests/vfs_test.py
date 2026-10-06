import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-vfs-') as tmp:
    disk = pathlib.Path(tmp) / 'disk.img'
    disk.write_bytes(bytes(2 * 1024 * 1024))
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '64', '-O', 'none,filetype', str(disk)], check=True)
    subprocess.run(['stdbuf', '-o0', './tests/vfs_test', str(disk)], check=True)
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
