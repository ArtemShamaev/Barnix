"""Build a fresh ext2 template with GCC applications, then replace it atomically."""
import pathlib
import subprocess
import sys
import tempfile

output = pathlib.Path(sys.argv[1]).resolve()
applications = [pathlib.Path(name).resolve() for name in sys.argv[2:]]
with tempfile.TemporaryDirectory(prefix='barnix-image-', dir=output.parent) as temporary:
    image = pathlib.Path(temporary) / 'live.img'
    with image.open('wb') as stream:
        stream.truncate(2 * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '64', '-O', 'none,filetype', str(image)], check=True)
    from install_apps import install
    install(image, applications)
    image.replace(output)
