"""Test device checks and format/mount sequencing without touching block devices."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import format_device as formatter


class FormatTests(unittest.TestCase):
    def setUp(self):
        self.target = {'path': '/dev/testdisk', 'pkname': None, 'type': 'disk',
                       'size': 16 * 1024**3, 'ro': False, 'mountpoints': [], 'maj:min': '999:999'}
        self.inventory = {'blockdevices': [self.target]}

    def inspect(self):
        return formatter.inspect_device(Path('/dev/testdisk'), self.inventory)

    def test_size_and_busy_descendants(self):
        self.assertEqual(self.inspect()[0], 5 * 1024 * 1024)
        child = dict(self.target, path='/dev/testdisk1', pkname='/dev/testdisk', type='part', mountpoints=['/'])
        self.inventory['blockdevices'].append(child)
        with self.assertRaisesRegex(ValueError, 'mounted'):
            self.inspect()
        child['mountpoints'] = ['[SWAP]']
        with self.assertRaisesRegex(ValueError, 'swap'):
            self.inspect()
        child['mountpoints'] = []
        child['type'] = 'lvm'
        with self.assertRaisesRegex(ValueError, 'mapping'):
            self.inspect()

    def test_read_only_small_and_unknown(self):
        self.target['ro'] = True
        with self.assertRaisesRegex(ValueError, 'Read-only'):
            self.inspect()
        self.target['ro'] = False
        self.target['size'] = 1024
        with self.assertRaisesRegex(ValueError, '2 MiB'):
            self.inspect()
        self.inventory['blockdevices'] = []
        with self.assertRaisesRegex(ValueError, 'identify'):
            self.inspect()

    def flow(self, work, fail=None, become_busy=False):
        calls = []
        reads = 0
        def run(arguments, **kwargs):
            nonlocal reads
            calls.append(arguments)
            if arguments[0] == 'lsblk':
                reads += 1
                data = copy.deepcopy(self.inventory)
                if become_busy and reads == 2:
                    data['blockdevices'][0]['mountpoints'] = ['/media/other']
                return SimpleNamespace(stdout=json.dumps(data))
            if arguments[0] == fail:
                raise subprocess.CalledProcessError(1, arguments)
            return SimpleNamespace(stdout='')
        with patch.object(formatter.os, 'geteuid', return_value=0), \
             patch.object(formatter, 'resolve_device', return_value=(Path('/dev/testdisk'), SimpleNamespace(st_rdev=os.makedev(999, 999)))), \
             patch.object(formatter.shutil, 'which', return_value='/tool'), \
             patch.object(formatter, 'run', side_effect=run), \
             patch.object(formatter.os, 'chown') as chown, \
             patch.dict(os.environ, {'SUDO_UID': '1000', 'SUDO_GID': '1000'}):
            try:
                formatter.format_device('/dev/testdisk', Path(work))
            except (ValueError, subprocess.CalledProcessError):
                self.calls = calls
                raise
            chown.assert_called_once_with(Path(work) / 'barnix-testdisk', 1000, 1000)
        return calls

    def test_formats_then_mounts_and_sets_owner(self):
        with tempfile.TemporaryDirectory() as work:
            calls = self.flow(work)
        self.assertEqual([c[0] for c in calls], ['lsblk', 'lsblk', 'wipefs', 'mke2fs', 'mount'])
        mkfs = calls[3]
        self.assertEqual(mkfs[-2:], ['/dev/testdisk', str(5 * 1024 * 1024)])
        self.assertIn('none,filetype,sparse_super', mkfs)
        self.assertNotIn('-q', mkfs)
        self.assertEqual(mkfs[mkfs.index('-E') + 1], 'nodiscard')
        self.assertIn('rw,nosuid,nodev', calls[-1])

    def test_failed_mkfs_does_not_mount(self):
        with tempfile.TemporaryDirectory() as work:
            with self.assertRaises(subprocess.CalledProcessError):
                self.flow(work, fail='mke2fs')
        self.assertNotIn('mount', [c[0] for c in self.calls])

    def test_failed_mount_reports_formatted_state(self):
        with tempfile.TemporaryDirectory() as work:
            with self.assertRaisesRegex(ValueError, 'was formatted, but mounting failed'):
                self.flow(work, fail='mount')

    def test_recheck_before_wiping(self):
        with tempfile.TemporaryDirectory() as work:
            with self.assertRaisesRegex(ValueError, 'mounted'):
                self.flow(work, become_busy=True)
        self.assertEqual([c[0] for c in self.calls], ['lsblk', 'lsblk'])

    def test_mountpoint_cannot_hide_files(self):
        with tempfile.TemporaryDirectory() as work:
            target = Path(work) / 'barnix-testdisk'
            target.mkdir()
            (target / 'keep').write_text('user data')
            with self.assertRaisesRegex(ValueError, 'empty'):
                self.flow(work)
            self.assertEqual((target / 'keep').read_text(), 'user data')
        self.assertEqual([c[0] for c in self.calls], ['lsblk'])

    def test_make_rejects_multiple_devices_and_missing_argument(self):
        for args in ([], ['/dev/a', '/dev/b'], ['clean']):
            result = subprocess.run(['make', '--no-print-directory', 'format', *args], cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('python3 scripts/format_device.py', result.stdout)
        with self.assertRaisesRegex(ValueError, 'wildcard'):
            formatter.format_device('/dev/*')
        with self.assertRaisesRegex(ValueError, 'block device'):
            formatter.resolve_device('/dev/null')
        result = subprocess.run(['make', '-n', '--no-print-directory', 'format', '/dev/testdisk'], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('python3 scripts/format_device.py', result.stdout)


if __name__ == '__main__':
    unittest.main()
