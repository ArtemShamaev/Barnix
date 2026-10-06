#!/usr/bin/env python3
"""Install a packaged BDK into a user prefix and register its dotnet templates."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local/share/barnino-bdk')
    parser.add_argument('--headers-only', action='store_true', help='Copy SDK without registering dotnet templates')
    parser.add_argument('--include-dir', type=Path, help='Additionally install public API headers in a compiler include directory')
    parser.add_argument('--dotnet', default='dotnet', help='Path to the .NET executable')
    parser.add_argument('--template-hive', type=Path, help='Isolated template registry for testing')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    if not (source / 'sdk').is_dir():
        raise ValueError('Run the installer from the packaged dist directory (make bdk first).')
    prefix = args.prefix.expanduser().resolve()
    if prefix == source or prefix == source / 'sdk':
        raise ValueError('Choose an installation prefix different from the package directory.')
    prefix.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source / 'sdk', prefix / 'sdk', dirs_exist_ok=True)
    if args.include_dir:
        # Do not replace the host stdio.h or iostream. Their BDK variants belong
        # exclusively to the freestanding SDK include directory.
        include = args.include_dir.expanduser().resolve()
        include.mkdir(parents=True, exist_ok=True)
        for name in ('barnix_api.h', 'barnix_api', 'barnix_graphics.h', 'barnix_graphics'):
            shutil.copy2(source / 'sdk/include' / name, include / name)
        shutil.copytree(source / 'sdk/include/barnix', include / 'barnix', dirs_exist_ok=True)
    packages = list(source.glob('Barnino.Systems.BDK.Templates.*.nupkg'))
    if len(packages) != 1:
        raise ValueError('Expected exactly one BDK template package in this directory.')
    target = prefix / packages[0].name
    shutil.copy2(packages[0], target)
    if not args.headers_only:
        hive = ['--debug:custom-hive', str(args.template_hive.resolve())] if args.template_hive else []
        installed = subprocess.run([args.dotnet, 'new', 'uninstall', *hive],
                                   check=True, capture_output=True, text=True).stdout
        if 'Barnino.Systems.BDK.Templates' in installed:
            subprocess.run([args.dotnet, 'new', 'uninstall', 'Barnino.Systems.BDK.Templates', *hive], check=True)
        subprocess.run([args.dotnet, 'new', 'install', str(target), *hive], check=True)
    print(f'BDK SDK: {prefix / "sdk"}')
    if not args.headers_only:
        print('Ready: dotnet new bca -o MyApp && cd MyApp && dotnet run')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'BDK installation failed: {error}', file=sys.stderr)
        sys.exit(1)
