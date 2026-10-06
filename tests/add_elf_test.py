"""Exercise Complete registration and offline raw/partitioned disk imports."""
import fcntl
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from add_elf import add, complete_apps


def debug(image, command):
    return subprocess.run(['debugfs', '-R', command, str(image)], check=True, capture_output=True, text=True)


with tempfile.TemporaryDirectory(prefix='barnix-add-test-') as temporary:
    work = Path(temporary)
    (work / 'Setup').mkdir()
    (work / 'disks').mkdir()
    shutil.copy2(ROOT / 'Setup/profiles.json', work / 'Setup/profiles.json')
    program = work / 'myapp.elf'
    shutil.copy2(ROOT / 'apps/hello.elf', program)
    disk = work / 'disks/WINDAGOVNO.img'
    disk.write_bytes(bytes(2 * 1024 * 1024))
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '64', '-O', 'none,filetype', str(disk)], check=True)
    note = work / 'note'
    note.write_bytes(b'keep my data\x00\xff')
    subprocess.run(['debugfs', '-w', '-R', f'write {note} /note', str(disk)], check=True, capture_output=True)
    partitioned = work / 'disks/partitioned.img'
    prefix = bytearray(1024 * 1024)
    prefix[510:512] = b'\x55\xaa'
    struct.pack_into('<B3sB3sII', prefix, 446, 0x80, bytes(3), 0x83, bytes(3), 2048, 4096)
    tail = b'preserve disk tail' * 32
    partitioned.write_bytes(prefix + disk.read_bytes() + tail)
    for _ in range(2):
        add(program, work)
        assert [p.name for p in complete_apps(work)] == ['myapp.elf']
        assert (work / 'Setup/extra-apps/myapp.elf').read_bytes() == program.read_bytes()
        contents = partitioned.read_bytes()
        assert contents[:len(prefix)] == prefix and contents[-len(tail):] == tail
        partition = work / 'extracted.ext2'
        partition.write_bytes(contents[len(prefix):-len(tail)])
        for image in (disk, partition):
            for name, expected in (('/note', note.read_bytes()), ('/bin/myapp.elf', program.read_bytes()), ('/myapp.elf', program.read_bytes())):
                output = work / 'output'
                debug(image, f'dump {name} {output}')
                assert output.read_bytes() == expected
                output.unlink()
            assert '0755' in debug(image, 'stat /bin/myapp.elf').stdout
    before = disk.read_bytes(), partitioned.read_bytes(), (work / 'Setup/extra-apps/myapp.elf').read_bytes()
    # A later bad disk must leave earlier disks and the saved application intact.
    (work / 'disks/zz-bad.img').write_bytes(b'not a filesystem')
    try:
        add(program, work)
        raise AssertionError('bad disk accepted')
    except ValueError:
        pass
    assert before == (disk.read_bytes(), partitioned.read_bytes(), (work / 'Setup/extra-apps/myapp.elf').read_bytes())
    (work / 'disks/zz-bad.img').unlink()
    program.write_bytes(b'not an ELF')
    try:
        add(program, work)
        raise AssertionError('bad ELF accepted')
    except subprocess.CalledProcessError:
        pass
    assert before == (disk.read_bytes(), partitioned.read_bytes(), (work / 'Setup/extra-apps/myapp.elf').read_bytes())
    shutil.copy2(ROOT / 'apps/hello.elf', program)
    # Lock in a separate process, like QEMU; use stdin to release deterministically.
    holder = subprocess.Popen([sys.executable, '-c',
        'import fcntl,sys; f=open(sys.argv[1],"r+b"); fcntl.lockf(f,fcntl.LOCK_EX); print("locked",flush=True); sys.stdin.read()', str(disk)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        assert holder.stdout.readline().strip() == 'locked'
        try:
            add(program, work)
            raise AssertionError('busy disk accepted')
        except ValueError as error:
            assert 'in use' in str(error)
    finally:
        holder.communicate('')
    assert before == (disk.read_bytes(), partitioned.read_bytes(), (work / 'Setup/extra-apps/myapp.elf').read_bytes())
    # No real disk is touched by the make argument smoke tests.
    failed = subprocess.run(['make', '--no-print-directory', 'add_elf', str(work / 'missing.elf')], cwd=ROOT, capture_output=True, text=True)
    assert failed.returncode and 'ELF does not exist' in failed.stderr, failed.stdout + failed.stderr
    failed = subprocess.run(['make', '--no-print-directory', 'add_elf', 'ELF=' + str(work / 'space path/missing.elf')], cwd=ROOT, capture_output=True, text=True)
    assert failed.returncode and 'ELF does not exist' in failed.stderr, failed.stdout + failed.stderr
print('add_elf: Complete, raw/MBR disks, repeat import, preserved files, invalid input and busy disks passed')
