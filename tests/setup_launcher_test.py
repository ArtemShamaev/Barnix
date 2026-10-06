"""Ensure Setup and disk-only boot select identical persistent destination images."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('setup_launcher', ROOT / 'Setup/run.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='barnix-launcher-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.here = self.root / 'Setup'
        self.here.mkdir()
        self.code = self.root / 'OVMF_CODE.fd'
        self.variables = self.root / 'OVMF_TEMPLATE.fd'
        self.code.write_bytes(b'firmware')
        self.variables.write_bytes(b'variables')
        for mode in ('legacy', 'efi'):
            (self.root / f'barnix-{mode}-setup.iso').write_bytes(b'installer')

    def launch(self, *arguments, environment=None):
        argv = ['run.py', '--ovmf-code', str(self.code), '--ovmf-vars', str(self.variables), *arguments]
        with patch.object(launcher, 'HERE', self.here), patch.object(sys, 'argv', argv), \
             patch.dict(os.environ, {'SETUP_DISK': environment or ''}), \
             patch.object(launcher.subprocess, 'run') as run, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            launcher.main()
            run.assert_called_once()
            return run.call_args.args[0]

    def test_default_destination_matches_and_disk_boot_needs_no_iso(self):
        for mode, name in [('efi', 'target-efi.img'), ('legacy', 'target.img')]:
            with self.subTest(mode=mode):
                setup = self.launch('--firmware', mode)
                target = self.here / name
                self.assertTrue(target.is_file())
                # Use a small sentinel after testing sparse creation; boot must preserve it.
                target.write_bytes(b'installed system contents')
                (self.root / f'barnix-{mode}-setup.iso').unlink()
                boot = self.launch('--firmware', mode, '--installed')
                drive = f'file={target},format=raw,if=ide,index=0'
                self.assertIn(drive, setup)
                self.assertIn(drive, boot)
                self.assertTrue(any('media=cdrom' in arg for arg in setup))
                self.assertFalse(any('media=cdrom' in arg for arg in boot))
                self.assertEqual(boot[boot.index('-boot') + 1], 'order=c,menu=on')
                self.assertEqual(target.read_bytes(), b'installed system contents')
                self.assertEqual(boot[0], 'qemu-system-x86_64' if mode == 'efi' else 'qemu-system-i386')
                if mode == 'efi':
                    self.assertEqual((self.here / 'OVMF_VARS.fd').read_bytes(), b'variables')
                    self.assertEqual([a for a in setup if 'pflash' in a], [a for a in boot if 'pflash' in a])

    def test_installed_mode_never_creates_missing_destination(self):
        for mode, name in [('legacy', 'target.img'), ('efi', 'target-efi.img')]:
            with self.assertRaises(SystemExit):
                self.launch('--firmware', mode, '--installed')
            self.assertFalse((self.here / name).exists())

    def test_explicit_disk_preserved_for_each_transport(self):
        target = self.root / 'custom disk.img'
        target.write_bytes(b'custom OS')
        for transport in ('ide', 'sata', 'nvme', 'virtio'):
            for installed in ([], ['--installed']):
                command = self.launch('--firmware', 'efi', '--transport', transport, '--disk', str(target), *installed)
                self.assertTrue(any(f'file={target},' in arg for arg in command))
                self.assertFalse(any('target-efi.img' in arg for arg in command))
                self.assertEqual(target.read_bytes(), b'custom OS')

    def test_make_disk_override_and_cli_precedence(self):
        target = self.root / 'selected.img'
        target.write_bytes(b'OS')
        for installed in ([], ['--installed']):
            command = self.launch('--firmware', 'efi', *installed, environment=str(target))
            self.assertTrue(any(f'file={target},' in arg for arg in command))
        command = self.launch('--disk', str(target), '--installed', environment='/does/not/exist.img')
        self.assertTrue(any(f'file={target},' in arg for arg in command))

    def test_make_run_efi_uses_installed_launcher_without_building_live_cd(self):
        result = subprocess.run(['make', '-n', '--no-print-directory', 'run', 'FIRMWARE=efi'], cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertIn('Setup/run.py --installed --firmware efi', result.stdout)
        self.assertNotIn('managed_disks.py', result.stdout)
        self.assertNotIn('grub-mkrescue', result.stdout)


if __name__ == '__main__':
    unittest.main()
