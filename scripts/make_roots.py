"""Write certs/roots.bin, the TLS trust store of Barnix, from a PEM bundle.

Usage: python3 scripts/make_roots.py [BUNDLE] [OUTPUT]
BUNDLE defaults to the system Mozilla bundle (/etc/ssl/certs/ca-certificates.crt).
Each record keeps what checking a chain needs: the subject name (matched
against issuers byte for byte) and the public key, as
u16 length + subject DER, u16 length + SubjectPublicKeyInfo DER (big-endian).
"""
import base64
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def element(data, at):
    """(start, contents start, end) of the DER element at at."""
    length = data[at + 1]
    header = 2
    if length & 0x80:
        count = length & 0x7f
        length = int.from_bytes(data[at + 2:at + 2 + count], 'big')
        header += count
    return at, at + header, at + header + length


def records(der):
    _, body, _ = element(der, 0)                  # Certificate
    _, tbs, tbs_end = element(der, body)          # TBSCertificate
    at = tbs
    if der[at] == 0xa0:                           # version
        at = element(der, at)[2]
    fields = []
    for _ in range(6):                            # serial, algorithm, issuer, validity, subject, key
        start, _, end = element(der, at)
        fields.append(der[start:end])
        at = end
    return fields[4], fields[5]


def main():
    bundle = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/etc/ssl/certs/ca-certificates.crt')
    output = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / 'certs' / 'roots.bin')
    text = bundle.read_text()
    out = bytearray()
    count = 0
    for block in re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----', text, re.S):
        der = base64.b64decode(''.join(block.split()))
        subject, key = records(der)
        out += len(subject).to_bytes(2, 'big') + subject + len(key).to_bytes(2, 'big') + key
        count += 1
    output.write_bytes(out)
    print(f'{count} roots, {len(out)} bytes')


if __name__ == '__main__':
    main()
