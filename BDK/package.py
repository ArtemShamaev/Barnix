#!/usr/bin/env python3
"""Assemble the standalone SDK and a local dotnet template NuGet package."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
VERSION = '0.1.0'
PACKAGE = f'Barnino.Systems.BDK.Templates.{VERSION}.nupkg'


def sdk(destination):
    shutil.copytree(ROOT / 'include', destination / 'include', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'runtime', destination / 'runtime', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'examples', destination / 'examples', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'tools', destination / 'tools', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', 'bin', 'obj'))
    headers = destination / 'include' / 'barnix'
    headers.mkdir(parents=True, exist_ok=True)
    for name in ('app_abi.h', 'mouse.h', 'keyboard.h'):
        shutil.copy2(REPO / name, headers / name)
    for name in ('text.c', 'text.h'):
        shutil.copy2(REPO / name, destination / 'runtime' / name)
    shutil.copy2(REPO / 'apps/runtime.c', destination / 'runtime/memory.c')
    shutil.copy2(REPO / 'barnixiolib/stdio.c', destination / 'runtime/stdio.c')
    stdio = (REPO / 'barnixiolib/stdio.h').read_text().replace('../app_abi.h', 'barnix/app_abi.h')
    # The existing C library is usable from both languages without name mangling.
    stdio = stdio.replace('/* A deliberately', '#ifdef __cplusplus\nextern "C" {\n#endif\n\n/* A deliberately')
    pos = stdio.rfind('#endif')
    stdio = stdio[:pos] + '#ifdef __cplusplus\n}\n#endif\n\n' + stdio[pos:]
    (destination / 'include/stdio.h').write_text(stdio)


def package(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='bdk-package-') as temporary:
        stage = Path(temporary)
        sdk(stage / 'sdk')
        for short, source, title in (
            ('bca', 'console', 'Barnix Console App'),
            ('bga', 'graphic', 'Barnix Graphic Library'),
            ('bcawms', 'mouse', 'Barnx Console App with Mouse Support'),
            ('bga_cs', 'graphic', 'Barnix Graphic Library (C#)'),
            ('bca_cs', 'console', 'Barnix Console App (C#)'),
            ('bcawms_cs', 'mouse', 'Barnix Console App with Mouse Support (C#)'),
        ):
            template = stage / 'content' / short
            (template / '.template.config').mkdir(parents=True)
            (template / 'src').mkdir()
            config = {
                '$schema': 'http://json.schemastore.org/template',
                'author': 'Barnino Systems', 'classifications': ['Barnix', 'Console', 'Native'],
                'identity': 'Barnino.Systems.BDK.' + short,
                'name': title, 'shortName': short, 'sourceName': 'BarnixProject',
                'preferNameDirectory': True, 'tags': {'language': 'C++', 'type': 'project'},
                'symbols': {'c': {'type': 'parameter', 'datatype': 'bool', 'defaultValue': 'false',
                                  'description': 'Create a C application instead of C++.'}},
                'sources': [{'copyOnly': ['bdk/**'], 'modifiers': [
                    {'condition': '(c)', 'exclude': ['src/main.cpp']},
                    {'condition': '(!c)', 'exclude': ['src/main.c']},
                ]}],
            }
            if source == 'graphic':
                config['classifications'] = ['Barnix', 'Graphics', 'Native']
            if short.endswith('_cs'):
                config['tags']['language'] = 'C#'
                config['symbols'] = {}
                config['sources'] = [{'copyOnly': ['bdk/**']}]
            (template / '.template.config/template.json').write_text(json.dumps(config, indent=2) + '\n')
            for name in ('Launcher.cs', 'BarnixProject.csproj'):
                shutil.copy2(ROOT / 'templates' / name, template / name)
            for extension in (('cs',) if short.endswith('_cs') else ('c', 'cpp')):
                shutil.copy2(ROOT / 'templates' / (source + '.' + extension), template / 'src' / ('main.' + extension))
            shutil.copytree(stage / 'sdk', template / 'bdk')
            shutil.copy2(ROOT / 'PROJECT.md', template / 'README.md')
            (template / '.gitignore').write_text('bin/\nobj/\nelf/\n__pycache__/\n')
            (template / 'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>\n')
        (stage / 'Barnino.Systems.BDK.Templates.nuspec').write_text(f'''<?xml version="1.0"?>
<package xmlns="http://schemas.microsoft.com/packaging/2010/07/nuspec.xsd">
  <metadata>
    <id>Barnino.Systems.BDK.Templates</id><version>{VERSION}</version>
    <authors>Barnino Systems</authors><description>Native Barnix C/C++/C# developer kit and console templates.</description>
    <packageTypes><packageType name="Template" /></packageTypes>
  </metadata>
</package>
''')
        (stage / '[Content_Types].xml').write_text('''<?xml version="1.0"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="nuspec" ContentType="application/octet" />
</Types>
''')
        with zipfile.ZipFile(output / PACKAGE, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(stage.rglob('*')):
                if path.is_file() and 'sdk' != path.relative_to(stage).parts[0]:
                    archive.write(path, path.relative_to(stage))
        shutil.copytree(stage / 'sdk', output / 'sdk', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'windows', output / 'windows', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', 'bin', 'obj'))
    iss = output / 'windows/bdk.iss'
    iss.write_text(iss.read_text().replace('..\\dist', '..'))
    for name in ('install.py', 'setup.sh', 'README.md', 'PROJECT.md'):
        shutil.copy2(ROOT / name, output / name)
    release = output / f'Barnino.Systems.BDK-{VERSION}.zip'
    with zipfile.ZipFile(release, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((output / 'sdk').rglob('*')):
            if path.is_file() and not {'bin', 'obj', '__pycache__'}.intersection(path.relative_to(output / 'sdk').parts):
                archive.write(path, path.relative_to(output))
        for path in sorted((output / 'windows').rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(output))
        for name in (PACKAGE, 'install.py', 'setup.sh', 'README.md', 'PROJECT.md'):
            archive.write(output / name, name)
    print(output / PACKAGE)
    print(release)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    package(parser.parse_args().output.resolve())
