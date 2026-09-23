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
    existing = debug('stat /bin')
    if 'Inode:' not in existing.stdout:
        debug('mkdir /bin')
    elif 'Type: directory' not in existing.stdout:
        raise ValueError('/bin exists and is not a directory')
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
        for app in map(pathlib.Path, programs):
            app = app.resolve()
            if any(c in str(app) for c in '\n\r"\\') or len(app.name) > 23:
                raise ValueError('Unsupported application pathname')
            destinations = [f'/bin/{app.name}']
            if app.suffix == '.elf':
                destinations.append('/' + app.name)
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
    subprocess.run(['e2fsck', '-fn', str(image)], check=True, capture_output=True)


if __name__ == '__main__':
    output = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='barnix-install-', dir=output.parent) as temporary:
        image = pathlib.Path(temporary) / 'disk.img'
        shutil.copyfile(output, image)
        install(image, sys.argv[2:])
        image.replace(output)
