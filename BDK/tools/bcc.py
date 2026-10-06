#!/usr/bin/env python3
"""Barnino C Compiler: compile one freestanding C source into a Barnix ELF.

BCC deliberately shares the BDK linker/runtime pipeline. It is a small,
portable compiler driver rather than a second C implementation: the selected
CC performs parsing/code generation while BCC supplies Barnix's ABI, headers,
runtime and ELF validation.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(prog="bcc", description=__doc__)
    parser.add_argument("source", type=Path, help="C source file")
    parser.add_argument("-o", "--output", type=Path, help="output ELF path")
    parser.add_argument("--name", help="Barnix filename when -o is omitted")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"), help="C compiler")
    args = parser.parse_args()
    source = args.source.resolve()
    if source.suffix != ".c":
        parser.error("BCC accepts a C source file (.c)")
    if not source.is_file():
        parser.error(f"source does not exist: {source}")
    if args.name and not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*\.elf", args.name):
        parser.error("--name must be a Barnix ELF filename")
    name = args.name or (source.stem + ".elf")
    if len(name) > 23:
        parser.error("Barnix filenames are limited to 23 characters")
    # Match the familiar command-line flow: `bcc main.c` creates ./main.elf.
    output = (args.output or Path(name)).resolve()
    sdk = Path(__file__).resolve().parents[1]
    build = sdk / "tools" / "build.py"
    if not build.is_file():
        parser.error("BDK tools/build.py is missing")
    with tempfile.TemporaryDirectory(prefix="bcc-") as temporary:
        project = Path(temporary)
        src = project / "src"
        src.mkdir()
        text = source.read_text(encoding="utf-8")
        if not re.search(r"#\s*define\s+BARNIX_APP_NAME\s+\"", text):
            text = f'#define BARNIX_APP_NAME "{name}"\n' + text
        (src / "main.c").write_text(text, encoding="utf-8")
        environment = dict(os.environ, CC=args.cc)
        try:
            subprocess.run([sys.executable, str(build), str(project)], check=True, env=environment)
        except subprocess.CalledProcessError as error:
            return error.returncode
        built = project / "elf" / name
        if not built.is_file():
            print(f"BCC: compiler produced no {name}", file=sys.stderr)
            return 1
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(built, output)
    print(f"BCC: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
