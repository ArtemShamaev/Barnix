"""Compile and run a C program using BCC inside the booted Barnix system."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from qemu_shell import run


with tempfile.TemporaryDirectory(prefix="barnix-bcc-native-") as temporary:
    directory = Path(temporary)
    disk = directory / "disk.img"
    shutil.copyfile(ROOT / "live-ext2.img", disk)
    source = directory / "main.c"
    source.write_text('#include <stdio.h>\nint main(void) { puts("Hello from self-hosted BCC"); return 0; }\n')
    invalid = directory / "invalid.c"
    invalid.write_text('int main(void) { printf("missing semicolon") return 0; }\n')
    subprocess.run(["debugfs", "-w", "-R", f'write "{source}" /main.c', str(disk)],
                   check=True, capture_output=True)
    subprocess.run(["debugfs", "-w", "-R", f'write "{invalid}" /invalid.c', str(disk)],
                   check=True, capture_output=True)
    run(disk, directory, [
        ("bcc invalid.c", "expected ';' after output call"),
        ("bcc main.c", "compiled native Barnix ELF"),
        ("./main.elf", "Hello from self-hosted BCC"),
    ])
print("Self-hosted BCC compiled and ran a C program in Barnix")
