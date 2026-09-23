"""Explicitly mark rebuilt legacy Barnix programs: standalone OSABI, ABI v5."""
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
data = bytearray(path.read_bytes())
if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
    raise ValueError('expected little-endian ELF32')
header = pathlib.Path(__file__).resolve().parent.parent / 'app_abi.h'
version = int(re.search(r'#define BARNIX_APP_ABI (\d+)U', header.read_text())[1])
data[7:9] = bytes([255, version])
path.write_bytes(data)
