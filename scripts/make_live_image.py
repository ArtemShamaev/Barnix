"""Build a fresh ext2 image with Barnix apps and optionally a native GCC toolchain."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gcc-build', type=pathlib.Path)
parser.add_argument('--binutils-build', type=pathlib.Path)
parser.add_argument('--host-root', type=pathlib.Path, default=pathlib.Path('/'))
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('applications', nargs='*', type=pathlib.Path)
args = parser.parse_args()
if bool(args.gcc_build) != bool(args.binutils_build):
    parser.error('--gcc-build and --binutils-build must be supplied together')
output = args.output.resolve()
applications = [name.resolve() for name in args.applications]
with tempfile.TemporaryDirectory(prefix='barnix-image-', dir=output.parent) as temporary:
    image = pathlib.Path(temporary) / 'live.img'
    with image.open('wb') as stream:
        stream.truncate((384 if args.gcc_build else 16) * 1024 * 1024)
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-N', '8192' if args.gcc_build else '256',
                    '-O', 'none,filetype', str(image)], check=True)
    from install_apps import install
    install(image, applications)
    from install_nova import install as install_nova
    install_nova(image, pathlib.Path(__file__).resolve().parent.parent / 'tools/nova/build/examples')
    from install_minecraft import install as install_minecraft
    install_minecraft(image)
    from install_firmware import install as install_firmware
    install_firmware(image)
    if args.gcc_build:
        from install_gcc_toolchain import install as install_gcc
        count = install_gcc(image, args.gcc_build, args.binutils_build,
                            args.host_root)
        print(f'Installed {count} GCC toolchain files')
        # brm: GCC came with this image (/etc/brm/base.d/gcc, /etc/brm/files/gcc)
        import subprocess as run
        from install_apps import write_text_files
        from system_packages import gcc_files_from_image, gcc_package, base_files
        def debug(command):
            return run.run(['debugfs', '-w', '-R', command, str(image)], check=True, capture_output=True, text=True)
        with tempfile.TemporaryDirectory(prefix='barnix-gcc-list-', dir=output.parent) as listing:
            package = gcc_package(gcc_files_from_image(image, listing))
            write_text_files(debug, pathlib.Path(listing), base_files([package], 'gcc'))
    image.replace(output)
