#!/usr/bin/env python3
"""Build streaming Barnix packages served by Setup/run.py over HTTP."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Setup/build/packages'


def archive(name, files):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = OUTPUT / (name + '.bpk')
    with path.with_suffix('.tmp').open('wb') as stream:
        stream.write(b'BPK1')
        for destination, (source, executable) in sorted(files.items()):
            encoded = destination.encode('ascii')
            size = source.stat().st_size
            if not destination.startswith('/') or '..' in Path(destination).parts or len(encoded) > 255 or size > 1024 * 1024 * 1024:
                raise ValueError(f'Unsafe package entry: {destination}')
            stream.write(struct.pack('<HII', len(encoded), 0o100755 if executable else 0o100644, size))
            stream.write(encoded)
            with source.open('rb') as source_stream:
                shutil.copyfileobj(source_stream, stream, 1024 * 1024)
        stream.write(struct.pack('<HII', 0, 0, 0))
    path.with_suffix('.tmp').replace(path)
    hash_state = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            hash_state.update(chunk)
    digest = hash_state.hexdigest()
    return f'{name} {digest}\n'


def registration(name, chosen, scratch):
    """/etc/brm/base.d/NAME (and bundle file lists) as package entries, so brm
    knows the component came with the system."""
    from sys import path as import_path
    import_path.insert(0, str(ROOT / 'scripts'))
    from system_packages import base_files
    files = {}
    for number, (destination, text) in enumerate(base_files(chosen, name).items()):
        source = Path(scratch) / f'{name}-registration-{number}'
        source.write_text(text, encoding='utf-8')
        files[destination] = (source, False)
    return files


def app_packages():
    manifest = json.loads((ROOT / 'Setup/profiles.json').read_text())['profiles']
    lines = []
    from sys import path as import_path
    import_path.insert(0, str(ROOT / 'scripts'))
    from system_packages import system_packages
    known = system_packages()
    scratch = tempfile.mkdtemp(prefix='barnix-registration-', dir=OUTPUT)
    for profile, name in ((manifest[1], 'network'), (manifest[2], 'games')):
        files = {f'/usr/bin/{command}': (ROOT / 'apps/bin' / command, True)
                 for command in profile['commands']}
        examples = [ROOT / 'apps' / example for example in profile.get('examples', [])]
        if name == 'games':
            from sys import path as import_path
            import_path.insert(0, str(ROOT / 'scripts'))
            from add_elf import complete_apps
            examples += complete_apps(ROOT)
        for source in examples:
            files[f'/usr/bin/{source.name}'] = (source, True)
            files[f'/{source.name}'] = (source, True)
        names = {Path(d).name for d in files if d.startswith('/usr/bin/')}
        chosen = [p for p in known if all(Path(d).name in names for d in p.files if d.startswith('/usr/bin/'))
                  and any(Path(d).name in set(profile['commands']) | {e.name for e in examples} for d in p.files)]
        files.update(registration(name, chosen, scratch))
        lines.append(archive(name, files))
    shutil.rmtree(scratch, ignore_errors=True)
    return lines


def gcc_package(gcc_build, binutils_build):
    from sys import path as import_path
    import_path.insert(0, str(ROOT / 'scripts'))
    from install_gcc_toolchain import toolchain_files, GCC_LIB, validate_elf
    files = toolchain_files(gcc_build, binutils_build)
    with tempfile.TemporaryDirectory(prefix='barnix-gcc-package-', dir=OUTPUT) as temporary:
        temp = Path(temporary)
        specs = temp / 'specs'
        specs.write_text('*link_emulation:\nelf_i386\n\n*libgcc:\n-lgcc -lgcc_eh\n\n'
                         '*self_spec:\n+ %{!shared:%{!static:-static}}\n\n')
        files[f'{GCC_LIB}/specs'] = (specs, False)
        for destination, (source, executable) in list(files.items()):
            if executable:
                validate_elf(source)
                target = temp / destination.replace('/', '_')
                shutil.copyfile(source, target)
                subprocess.run(['strip', '--strip-all', str(target)], check=True)
                files[destination] = (target, True)
        return archive('gcc', files)


def gcc_package_from_image(image):
    """Reuse the already validated GCC Live filesystem for the normal installer."""
    if not image.is_file():
        raise FileNotFoundError(f'{image}: build live-ext2-gcc.img first')
    with tempfile.TemporaryDirectory(prefix='barnix-gcc-image-', dir=OUTPUT) as temporary:
        temporary = Path(temporary)
        subprocess.run(['debugfs', '-R', f'rdump /usr {temporary}', str(image)],
                       check=True, capture_output=True, text=True)
        root = temporary / 'usr'
        # /usr/bin also holds the Barnix commands; those belong to the base
        # system, and the installer refuses to overwrite existing files.
        barnix = {path.name for path in (ROOT / 'apps/bin').iterdir()} | \
                 {path.name for path in ROOT.glob('apps/*.elf')} | \
                 {path.name for path in (ROOT / 'Setup/extra-apps').glob('*.elf')}
        files = {}
        for part in ('bin', 'include', 'lib', 'libexec'):
            for source in (root / part).rglob('*'):
                if part == 'bin' and source.name in barnix:
                    continue
                if source.is_file():
                    destination = '/' + str(source.relative_to(temporary))
                    files[destination] = (source, part in ('bin', 'libexec'))
        for required in ('/usr/bin/gcc', '/usr/bin/g++', '/usr/bin/as', '/usr/bin/ld'):
            if required not in files:
                raise FileNotFoundError(f'GCC image lacks {required}')
        from sys import path as import_path
        import_path.insert(0, str(ROOT / 'scripts'))
        from system_packages import GCC_LIB, GCC_SPECS, gcc_package
        specs = temporary / 'static-specs'
        specs.write_text(GCC_SPECS)
        files[f'{GCC_LIB}/specs'] = (specs, False)
        files.update(registration('gcc', [gcc_package(dict(files))], temporary))
        return archive('gcc', files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gcc-build', type=Path)
    parser.add_argument('--binutils-build', type=Path)
    parser.add_argument('--gcc-image', type=Path)
    args = parser.parse_args()
    if bool(args.gcc_build) != bool(args.binutils_build):
        parser.error('both GCC and binutils build directories are required')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = OUTPUT / 'sha256sums'
    lines = app_packages()
    if args.gcc_image:
        lines.append(gcc_package_from_image(args.gcc_image))
    elif args.gcc_build:
        lines.append(gcc_package(args.gcc_build, args.binutils_build))
    manifest.write_text(''.join(lines))
    print('Built package repository:', ', '.join(line.split()[0] for line in lines))


if __name__ == '__main__':
    main()
