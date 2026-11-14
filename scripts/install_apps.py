"""Install applications into an offline image, validating a temporary copy first."""
import pathlib
import shutil
import subprocess
import sys
import tempfile


def install(image, programs):
    def debug(command):
        return subprocess.run(['debugfs', '-w', '-R', command, str(image)],
                              check=True, capture_output=True, text=True)
    # Everyone's scratch folder (Python's tempfile, GCC): sticky, world-writable.
    if 'Inode:' not in debug('stat /tmp').stdout:
        debug('mkdir /tmp')
        debug('set_inode_field /tmp mode 041777')
    # System commands live in /usr/bin. /bin stays (empty) for user programs and
    # is still searched after /usr/bin, so older disks keep working.
    for directory in ('/bin', '/usr', '/usr/bin'):
        existing = debug(f'stat {directory}')
        if 'Inode:' not in existing.stdout:
            debug(f'mkdir {directory}')
        elif 'Type: directory' not in existing.stdout:
            raise ValueError(f'{directory} exists and is not a directory')
    with tempfile.TemporaryDirectory(prefix='barnix-verify-') as temporary:
        existing = debug('stat /etc')
        if 'Inode:' not in existing.stdout:
            debug('mkdir /etc')
        elif 'Type: directory' not in existing.stdout:
            raise ValueError('/etc exists and is not a directory')
        if 'Inode:' not in debug('stat /etc/sys-lang.cfg').stdout:
            config = pathlib.Path(temporary) / 'sys-lang.cfg'
            config.write_text('LANG=en\n')
            imported = debug(f'write "{config}" /etc/sys-lang.cfg')
            if 'Allocated inode:' not in imported.stdout:
                raise RuntimeError(imported.stdout + imported.stderr)
        # Trust store for HTTPS (scripts/make_roots.py), read by BDK Barnix Net.
        roots = pathlib.Path(__file__).resolve().parent.parent / 'certs' / 'roots.bin'
        if roots.exists():
            if 'Inode:' not in debug('stat /etc/ssl').stdout:
                debug('mkdir /etc/ssl')
            debug('rm /etc/ssl/roots')
            imported = debug(f'write "{roots}" /etc/ssl/roots')
            if 'Allocated inode:' not in imported.stdout:
                raise RuntimeError(imported.stdout + imported.stderr)
        # Fonts of the Gouse Web browser (scripts/web_font.py).
        font = pathlib.Path(__file__).resolve().parent.parent / 'apps' / 'gouse' / 'web.bwf'
        if font.exists():
            for directory in ('/usr', '/usr/share', '/usr/share/gouse'):
                if 'Inode:' not in debug(f'stat {directory}').stdout:
                    debug(f'mkdir {directory}')
            debug('rm /usr/share/gouse/web.bwf')
            imported = debug(f'write "{font}" /usr/share/gouse/web.bwf')
            if 'Allocated inode:' not in imported.stdout:
                raise RuntimeError(imported.stdout + imported.stderr)
        # Barnix Code's Python debugger.
        helper = pathlib.Path(__file__).resolve().parent.parent / 'apps/gouse/debug.py'
        if helper.exists():
            debug('rm /usr/share/gouse/debug.py')
            imported = debug(f'write "{helper}" /usr/share/gouse/debug.py')
            if 'Allocated inode:' not in imported.stdout:
                raise RuntimeError(imported.stdout + imported.stderr)
        examples = pathlib.Path(__file__).resolve().parent.parent / 'apps/gouse/examples'
        if examples.exists():
            target = '/usr/share/gouse/examples'
            if 'Inode:' not in debug(f'stat {target}').stdout:
                debug(f'mkdir {target}')
            for example in sorted(examples.iterdir()):
                if example.suffix not in ('.html', '.js', '.txt'):
                    continue
                debug(f'rm {target}/{example.name}')
                imported = debug(f'write "{example}" {target}/{example.name}')
                if 'Allocated inode:' not in imported.stdout:
                    raise RuntimeError(imported.stdout + imported.stderr)
        # The manuals (Gouse Web shows them: Help in the IDE, the home page).
        manual = pathlib.Path(__file__).resolve().parent.parent / 'docs/manual'
        if manual.exists():
            for directory in ('/usr', '/usr/share', '/usr/share/doc', '/usr/share/doc/barnix'):
                if 'Inode:' not in debug(f'stat {directory}').stdout:
                    debug(f'mkdir {directory}')
            for page in sorted(manual.iterdir()):
                if not page.is_file():
                    continue
                debug(f'rm /usr/share/doc/barnix/{page.name}')
                imported = debug(f'write "{page}" /usr/share/doc/barnix/{page.name}')
                if 'Allocated inode:' not in imported.stdout:
                    raise RuntimeError(imported.stdout + imported.stderr)
        for app in map(pathlib.Path, programs):
            app = app.resolve()
            if any(c in str(app) for c in '\n\r"\\') or len(app.name) > 23:
                raise ValueError('Unsupported application pathname')
            destinations = [f'/usr/bin/{app.name}']
            if app.suffix == '.elf':
                destinations.append('/' + app.name)
            # Migrate images built before /usr/bin: drop the stale /bin copy.
            debug(f'rm /bin/{app.name}')
            for destination in destinations:
                debug(f'rm {destination}')
                result = debug(f'write "{app}" {destination}')
                if 'Allocated inode:' not in result.stdout:
                    raise RuntimeError(result.stdout + result.stderr)
                debug(f'set_inode_field {destination} mode 0100755')
                dumped = pathlib.Path(temporary) / 'verify'
                debug(f'dump {destination} "{dumped}"')
                if dumped.read_bytes() != app.read_bytes():
                    raise RuntimeError('Application import verification failed')
                dumped.unlink()
        register(debug, pathlib.Path(temporary), [pathlib.Path(p).name for p in programs])
    subprocess.run(['e2fsck', '-fn', str(image)], check=True, capture_output=True)


def write_text_files(debug, scratch, files):
    """{destination: text} into the image, folders included."""
    for destination, text in files.items():
        parts = destination.split('/')[1:-1]
        for i in range(1, len(parts) + 1):
            directory = '/' + '/'.join(parts[:i])
            if 'Inode:' not in debug(f'stat {directory}').stdout:
                debug(f'mkdir {directory}')
        source = scratch / 'registration'
        source.write_text(text, encoding='utf-8')
        debug(f'rm {destination}')
        imported = debug(f'write "{source}" {destination}')
        if 'Allocated inode:' not in imported.stdout:
            raise RuntimeError(imported.stdout + imported.stderr)


def register(debug, scratch, names, part='system'):
    """Tells brm which of its packages this image has: those whose programs
    were all installed (/etc/brm/base.d/system)."""
    from system_packages import system_packages, base_files
    installed = set(names)
    chosen = [p for p in system_packages()
              if all(pathlib.PurePosixPath(d).name in installed for d in p.files if d.startswith('/usr/bin/'))]
    write_text_files(debug, scratch, base_files(chosen, part))


if __name__ == '__main__':
    output = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='barnix-install-', dir=output.parent) as temporary:
        image = pathlib.Path(temporary) / 'disk.img'
        shutil.copyfile(output, image)
        install(image, sys.argv[2:])
        image.replace(output)
