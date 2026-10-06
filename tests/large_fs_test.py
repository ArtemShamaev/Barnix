from pathlib import Path
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-5g-') as temporary:
    disk = Path(temporary) / 'large.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024**3)
    subprocess.run(['stdbuf', '-o0', './tests/large_fs_test', str(disk)], check=True)
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    result = subprocess.run(['debugfs', '-R', 'cat /many/f159', str(disk)], check=True, capture_output=True, text=True)
    assert result.stdout == 'updated'
    with disk.open('rb') as stream:
        stream.seek(1028)
        assert int.from_bytes(stream.read(4), 'little') == 5 * 1024 * 1024
        stream.seek(5 * 1024**3)
        assert stream.read(4096) == bytes(4096)
