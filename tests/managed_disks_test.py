"""Exercise the public make syntax and inspect QEMU arguments without a VM."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='barnix-images-') as tmp:
    root = Path(tmp)
    (root / 'scripts').mkdir()
    shutil.copy(repo / 'scripts/managed_disks.py', root / 'scripts')
    command = ['make', '-f', str(repo / 'makefile'), 'img']
    subprocess.run([*command, 'work'], cwd=root, check=True)
    before = (root / 'disks/work.img').read_bytes()
    assert subprocess.run([*command, 'work'], cwd=root, capture_output=True).returncode != 0
    assert before == (root / 'disks/work.img').read_bytes()
    subprocess.run([*command, 'backup.img'], cwd=root, check=True)
    subprocess.run([*command, 'third', 'SIZE_MIB=5120'], cwd=root, check=True)
    assert (root / 'disks/third.img').stat().st_size == 5 * 1024**3
    assert subprocess.run([*command, 'invalid', 'SIZE_MIB=5121'], cwd=root, capture_output=True).returncode != 0
    assert not (root / 'disks/invalid.img').exists()
    disks = json.loads((root / 'disks/registry.json').read_text())
    assert disks == ['disks/work.img', 'disks/backup.img', 'disks/third.img']
    for disk in disks:
        subprocess.run(['e2fsck', '-fn', str(root / disk)], check=True, capture_output=True)
    fake = root / 'qemu-system-i386'
    fake.write_text('#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n')
    fake.chmod(0o755)
    env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'])
    result = subprocess.run(['python3', 'scripts/managed_disks.py', 'run', '-m', '256M'],
                            cwd=root, env=env, check=True, capture_output=True, text=True)
    args = json.loads(result.stdout)
    assert args == ['-m', '256M', '-drive', 'file=disks/work.img,format=raw,if=ide,index=1',
                    '-drive', 'file=disks/backup.img,format=raw,if=ide,index=2',
                    '-drive', 'file=disks/third.img,format=raw,if=none,id=extra3',
                    '-device', 'virtio-blk-pci,drive=extra3,disable-legacy=on']
print('make img and automatic QEMU attachment tests passed')
