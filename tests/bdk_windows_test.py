"""Run with Windows Python (native Windows or Wine) and an installed LLVM tree."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    env = dict(os.environ, CC=str(args.llvm / 'bin/clang.exe'),
               CXX=str(args.llvm / 'bin/clang++.exe'), BDK_LD=str(args.llvm / 'bin/ld.lld.exe'))
    sdk = ROOT / 'BDK/dist/sdk'
    artifacts = args.output / 'artifacts'; artifacts.mkdir(parents=True, exist_ok=True)
    for template, source in [('bca','console'),('bcawms','mouse')]:
        for extension in ('c','cpp'):
            project = args.output / (template + ' ' + extension)
            (project / 'src').mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / 'BDK/templates' / (source + '.' + extension), project / 'src' / ('main.' + extension))
            subprocess.run([sys.executable, str(sdk / 'tools/build.py'), str(project)], env=env, check=True)
            data = (project / 'elf/app.elf').read_bytes()
            assert data[:9] == b'\x7fELF\x01\x01\x01\xff\x09'
            shutil.copy2(project / 'elf/app.elf', artifacts / (template + '-' + extension + '.elf'))
    project = args.output / 'runtime'; (project / 'src').mkdir(parents=True, exist_ok=True)
    (project / 'src/main.cpp').write_text('''#define BARNIX_APP_NAME "runtime.elf"
#include <barnix_api>
#include <stdio.h>
#include <iostream>
using namespace Barnix;
static int initialized;
struct Init { Init() { initialized=42; } ~Init() { Console::WriteLine("BDK destructor OK"); } };
static Init init;
struct Local { ~Local() { Console::WriteLine("BDK local destructor OK"); } };
int main() {
    static Local local;
    if (initialized != 42) return 91;
    std::cout << "BDK runtime OK " << initialized << std::endl;
    printf("BDK stdio %d\\n", 7);
    return 0;
}
''')
    subprocess.run([sys.executable, str(sdk / 'tools/build.py'), str(project)], env=env, check=True)
    shutil.copy2(project / 'elf/runtime.elf', artifacts / 'runtime.elf')
    print('Windows Python + LLVM: C/C++ console/mouse templates and runtime compiled successfully')


if __name__ == '__main__':
    main()
