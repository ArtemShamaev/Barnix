"""Test packaged SDK, real dotnet templates, native ELF and the public API."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'BDK/dist'
ARTIFACTS = DIST / 'test-apps'


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def main():
    ARTIFACTS.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='barnix-bdk-') as temporary:
        work = Path(temporary)
        env = dict(os.environ, DOTNET_CLI_HOME=str(work / 'dotnet'),
                   DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_TELEMETRY_OPTOUT='1',
                   DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
        hive = ['--debug:custom-hive', str(work / 'hive')]
        sdk = DIST / 'sdk'
        objects = []
        for name in ('bdk.c', 'text.c', 'stdio.c'):
            obj = work / (name + '.o')
            run(['gcc', '-std=c11', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I', sdk / 'include',
                 '-I', sdk / 'runtime', '-c', sdk / 'runtime' / name, '-o', obj])
            objects.append(obj)
        api_test = work / 'api-test'
        run(['g++', '-std=c++17', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I', sdk / 'include',
             ROOT / 'tests/bdk/api_test.cpp', *objects, '-o', api_test])
        run([api_test])
        graphics_test = work / 'graphics-test'
        run(['gcc', '-std=c11', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-I', sdk / 'include', '-I', sdk / 'runtime',
             ROOT / 'tests/bdk/graphics_test.c', sdk / 'runtime/graphics.c',
             sdk / 'runtime/text.c', '-o', graphics_test])
        run([graphics_test], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        # Package is installed from a copy, so no repository paths can be needed.
        package = work / 'BDK.nupkg'
        shutil.copyfile(DIST / 'Barnino.Systems.BDK.Templates.0.1.0.nupkg', package)
        run(['dotnet', 'new', 'install', package, *hive], env=env)
        elfs = []
        for template in ('bca', 'bcawms', 'bca_cs', 'bcawms_cs', 'bga', 'bga_cs'):
            for language in (('cs',) if template.endswith('_cs') else ('cpp', 'c')):
                project = work / f'{template} {language}' # Paths with spaces must work.
                run(['dotnet', 'new', template, '-o', project, '--no-update-check', *hive,
                     *(['--c'] if language == 'c' else [])], env=env)
                assert (project / 'src' / f'main.{language}').is_file()
                assert len(list((project / 'src').glob('main.*'))) == 1
                assert (project / 'bdk/include/barnix_api.h').read_bytes() == (sdk / 'include/barnix_api.h').read_bytes()
                run(['dotnet', 'run', '--project', project], env=env)
                binary = project / 'elf/app.elf'
                data = binary.read_bytes()
                assert data[:9] == b'\x7fELF\x01\x01\x01\xff\x09'
                assert not run(['nm', '-u', binary], capture_output=True, text=True).stdout.strip()
                exported = ARTIFACTS / f'{template}-{language}.elf'
                shutil.copyfile(binary, exported); elfs.append(exported)
                # Name is controlled in code, not in the .NET launcher.
                main_source = project / 'src' / f'main.{language}'
                source = main_source.read_text()
                main_source.write_text(source.replace('"app.elf"', '"renamed.elf"'))
                run(['dotnet', 'run', '--no-build', '--project', project], env=env)
                renamed = project / 'elf/renamed.elf'
                assert renamed.is_file()
                before = renamed.read_bytes()
                main_source.write_text(main_source.read_text() + '\n#error intentional test failure\n')
                failed = subprocess.run(['dotnet', 'run', '--no-build', '--project', str(project)],
                                        env=env, capture_output=True, text=True)
                assert failed.returncode and ('intentional test failure' in failed.stderr or 'preprocessor directives' in failed.stderr)
                assert renamed.read_bytes() == before
                main_source.write_text(source.replace('"app.elf"', '"../escape.elf"'))
                failed = subprocess.run(['python3', str(project / 'bdk/tools/build.py'), str(project)],
                                        capture_output=True, text=True)
                assert failed.returncode and 'a filename, not a path' in failed.stderr
                main_source.write_text(source.replace('"app.elf"', '"' + 'a' * 24 + '.elf"'))
                failed = subprocess.run(['python3', str(project / 'bdk/tools/build.py'), str(project)],
                                        capture_output=True, text=True)
                assert failed.returncode and '23 characters' in failed.stderr
                print(f'BDK template, name and failure checks passed: {template}/{language}', flush=True)
        project = work / 'bca_cs cs'
        main_source = project / 'src/main.cs'
        program = """using Barnix;
static partial class Program {
    const string AppName = "csruntime.elf";
    static int Main() {
        int sum = 0;
        for (int i = 0; i < 5; i++) { if (i == 2) continue; sum += Twice(i); }
        int n = 2;
        while (n > 0) { sum++; n--; }
        if (sum != 18) return 91;
        Console.Write("BDK C# runtime OK "); Console.WriteLine(sum);
        return 0;
    }
}
"""
        main_source.write_text(program)
        (project / 'src/helper.cs').write_text('static partial class Program { static int Twice(int x) { return x * 2; } }')
        run(['dotnet', 'run', '--no-build', '--project', project], env=env)
        binary = project / 'elf/csruntime.elf'
        before = binary.read_bytes()
        exported = ARTIFACTS / 'csruntime.elf'
        shutil.copyfile(binary, exported); elfs.append(exported)
        for invalid, message in (
            ('int sum = "bad";', 'CS0029'),
            ('int[] sum = new int[2];', 'unsupported native C#'),
            ('int sum = 0; throw new System.Exception();', 'unsupported native C#'),
            ('int sum = "a" == "b" ? 0 : 1;', 'unsupported native C#'),
        ):
            main_source.write_text(program.replace('int sum = 0;', invalid).replace('sum += Twice(i);', '').replace('sum++;', '').replace('if (sum != 18) return 91;', '').replace('Console.WriteLine(sum);', 'Console.WriteLine();'))
            failed = subprocess.run(['dotnet', 'run', '--no-build', '--project', str(project)], env=env, capture_output=True, text=True)
            assert failed.returncode and message in failed.stderr, failed.stdout + failed.stderr
            assert binary.read_bytes() == before
        main_source.write_text(program)
        # A mixed C/C++ program, static initialization, stdio and the small iostream.
        project = work / 'bca cpp'
        (project / 'src/main.cpp').write_text('''#define BARNIX_APP_NAME "runtime.elf"
#include <barnix_api>
#include <stdio.h>
#include <iostream>
using namespace Barnix;
extern "C" int c_helper(void);
static int initialized;
struct Init { Init() { initialized = 42; } ~Init() { Console::WriteLine("BDK destructor OK"); } };
static Init init;
struct Local { ~Local() { Console::WriteLine("BDK local destructor OK"); } };
int main() {
    static Local local;
    if (initialized != 42 || c_helper() != 7) return 91;
    std::cout << "BDK runtime OK " << initialized << std::endl;
    printf("BDK stdio %d\\n", c_helper());
    return 0;
}
''')
        (project / 'src/helper.c').write_text('int c_helper(void) { return 7; }\n')
        run(['dotnet', 'run', '--no-build', '--project', project], env=env)
        exported = ARTIFACTS / 'runtime.elf'
        shutil.copyfile(project / 'elf/runtime.elf', exported); elfs.append(exported)
        validator = work / 'elf-test'
        run(['gcc', '-Wall', '-Wextra', '-I', ROOT, ROOT / 'tests/elf_test.c', '-o', validator])
        run([validator, *elfs], cwd=ROOT)
        # Test the independently distributable archive and optional include installation.
        release = work / 'release'
        with zipfile.ZipFile(DIST / 'Barnino.Systems.BDK-0.1.0.zip') as archive:
            archive.extractall(release)
        run(['python3', release / 'install.py', '--prefix', work / 'installed', '--headers-only',
             '--include-dir', work / 'include'])
        assert (work / 'include/barnix_api.h').is_file()
        assert (work / 'include/barnix_api').is_file()
        assert (work / 'include/barnix_graphics.h').is_file()
        assert (work / 'include/barnix_graphics').is_file()
        assert not (work / 'include/stdio.h').exists() and not (work / 'include/iostream').exists()
    print('BDK API, C/C++/C# templates, standalone builds and ELF validation passed', flush=True)


if __name__ == '__main__':
    main()
