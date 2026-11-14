"""Install host-compiled Nova examples into a Barnix ext2 image atomically."""
import pathlib
import shutil
import subprocess
import sys
import tempfile


def debug(image, command):
    return subprocess.run(
        ['debugfs', '-w', '-R', command, str(image)],
        check=True, capture_output=True, text=True,
    )


def install(image, examples):
    sources = pathlib.Path(__file__).resolve().parent.parent / 'tools/nova/examples'
    jars = sorted(examples.glob('*.jar'))
    if not jars:
        raise ValueError('Build the Nova examples first with make nova')
    for directory in ('/usr', '/usr/share', '/usr/share/nova', '/usr/share/nova/examples'):
        if 'Inode:' not in debug(image, f'stat {directory}').stdout:
            debug(image, f'mkdir {directory}')
    with tempfile.TemporaryDirectory(prefix='barnix-nova-verify-') as temporary:
        for jar in jars:
            src = sources / (jar.stem + '.nova')
            if not src.is_file():
                raise ValueError(f'Missing Nova source for {jar.name}')
            for file in (src, jar):
                if not file.name.replace('.', '').isalnum():
                    raise ValueError(f'Unsupported filename: {file.name}')
                destination = f'/usr/share/nova/examples/{file.name}'
                if 'Inode:' in debug(image, f'stat {destination}').stdout:
                    debug(image, f'rm {destination}')
                result = debug(image, f'write "{file.resolve()}" {destination}')
                if 'Allocated inode:' not in result.stdout:
                    raise RuntimeError(result.stdout + result.stderr)
                dumped = pathlib.Path(temporary) / 'verify'
                debug(image, f'dump {destination} "{dumped}"')
                if dumped.read_bytes() != file.read_bytes():
                    raise RuntimeError(f'Nova example import failed: {file.name}')
                dumped.unlink()
    subprocess.run(['e2fsck', '-fn', str(image)], check=True, capture_output=True)


if __name__ == '__main__':
    target = pathlib.Path(sys.argv[1]).resolve()
    examples = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix='barnix-nova-', dir=target.parent) as temporary:
        candidate = pathlib.Path(temporary) / 'disk.img'
        shutil.copyfile(target, candidate)
        install(candidate, examples)
        candidate.replace(target)
