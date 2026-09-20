"""Cross-check Barnix's filesystem implementation with e2fsprogs."""
import pathlib
import subprocess
import tempfile


def call(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout, result.stderr)
        result.check_returncode()
    return result


with tempfile.TemporaryDirectory(prefix='barnix-ext2-') as tmp:
    directory = pathlib.Path(tmp)
    host = directory / 'host.txt'
    host.write_text('from-linux')
    large = directory / 'large.txt'
    large.write_text('L' * 256)
    for revision, inode_size, features in [('0', '128', 'none'),
                                           ('1', '128', 'none,filetype'),
                                           ('1', '256', 'none,filetype,sparse_super,large_file')]:
        disk = directory / 'disk.img'
        with disk.open('wb') as image:
            image.truncate(2 * 1024 * 1024)
        call('mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', inode_size,
             '-N', '64', '-O', features, str(disk))
        if revision == '0':
            call('debugfs', '-w', '-R', 'set_super_value rev_level 0', str(disk))
        call('debugfs', '-w', '-R', f'write {host} /host', str(disk))
        call('debugfs', '-w', '-R', f'write {large} /large', str(disk))
        call('debugfs', '-w', '-R', 'symlink /link host', str(disk))
        print(call('stdbuf', '-o0', './tests/fs_test', str(disk)).stdout, end='')
        print(call('e2fsck', '-fn', str(disk)).stdout, end='')
        assert call('debugfs', '-R', 'cat /c', str(disk)).stdout == 'hello world'
        assert call('debugfs', '-R', 'cat /kept/nested', str(disk)).stdout == 'ext2'
        assert call('debugfs', '-R', 'cat /host', str(disk)).stdout == 'from-linux'
        assert call('debugfs', '-R', 'cat /large', str(disk)).stdout == 'L' * 256
    print('e2fsprogs interoperability tests passed (rev0/rev1, 128/256-byte inodes)')
