#!/usr/bin/env python3
"""Create a formatted Barnix USB-disk image and optionally install ELF apps."""
import argparse
import pathlib
import subprocess
import tempfile


def run(command):
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(
        description='Create a virtual Barnix USB flash drive formatted as ext2')
    parser.add_argument('output', type=pathlib.Path,
                        help='image to create, for example barnix-usb.img')
    parser.add_argument('programs', nargs='*', type=pathlib.Path,
                        help='ELF programs to copy into the image root')
    parser.add_argument('--size', type=int, default=2,
                        help='filesystem size in MiB (default: 2; maximum supported by Barnix)')
    parser.add_argument('--force', action='store_true',
                        help='replace an existing image')
    args = parser.parse_args()

    if args.size != 2:
        parser.error('Barnix currently supports a 2 MiB USB filesystem')
    output = args.output.resolve()
    if output.exists() and not args.force:
        parser.error(f'{output} already exists; use --force to reformat it')
    output.parent.mkdir(parents=True, exist_ok=True)

    programs = args.programs or sorted(pathlib.Path('apps').glob('*.elf')) + sorted(pathlib.Path('apps/bin').glob('*'))
    for program in programs:
        program = program.resolve()
        if not program.is_file():
            parser.error(f'program not found: {program}')
        if len(program.name) > 23 or any(c in program.name for c in '/\"\n\r'):
            parser.error(f'unsupported program name: {program.name}')

    with tempfile.TemporaryDirectory(prefix='barnix-usb-', dir=output.parent) as work:
        image = pathlib.Path(work) / 'usb.img'
        with image.open('wb') as stream:
            stream.truncate(args.size * 1024 * 1024)
        run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
             '-N', '64', '-O', 'none,filetype', str(image)])
        from install_apps import install
        install(image, programs)
        image.replace(output)


if __name__ == '__main__':
    main()
