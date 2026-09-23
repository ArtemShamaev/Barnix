"""Repeated offline installs preserve user bytes and validate command imports."""
import pathlib
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='barnix-upgrade-') as tmp:
    work = pathlib.Path(tmp)
    disk = work / 'disk.img'
    disk.write_bytes(bytes(2 * 1024 * 1024))
    note = work / 'note'
    note.write_bytes(b'preserve me\x00data')
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I',
                    '128', '-N', '64', '-O', 'none,filetype', str(disk)], check=True)
    subprocess.run(['debugfs', '-w', '-R', f'write {note} /note', str(disk)],
                   check=True, capture_output=True)
    programs = sorted(map(str, pathlib.Path('apps/bin').iterdir()))
    programs += ['apps/hello.elf', 'apps/fileio.elf']
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk), *programs], check=True)
    config = work / 'sys-lang.cfg'
    config.write_text('LANG=ru\n')
    for command in ['rm /etc/sys-lang.cfg', f'write {config} /etc/sys-lang.cfg']:
        subprocess.run(['debugfs', '-w', '-R', command, str(disk)], check=True, capture_output=True)
    for _ in range(2):
        subprocess.run(['python3', 'scripts/install_apps.py', str(disk), *programs], check=True)
    exported = work / 'out'
    subprocess.run(['debugfs', '-R', f'dump /note {exported}', str(disk)],
                   check=True, capture_output=True)
    assert exported.read_bytes() == note.read_bytes()
    saved_config = work / 'saved-config'
    subprocess.run(['debugfs', '-R', f'dump /etc/sys-lang.cfg {saved_config}', str(disk)], check=True, capture_output=True)
    assert saved_config.read_bytes() == config.read_bytes()
    before = disk.read_bytes()
    failed = subprocess.run(['python3', 'scripts/install_apps.py', str(disk),
                             str(work / 'missing')], capture_output=True)
    assert failed.returncode != 0 and disk.read_bytes() == before
    print('Offline installation: repeat update, user data and rollback passed')
