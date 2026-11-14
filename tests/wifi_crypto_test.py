"""Checks wifi_crypto.c against hashlib/cryptography and the 802.11i vector."""
import hashlib, hmac, os, random, subprocess, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.keywrap import aes_key_wrap

binary = sys.argv[1]
p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
def ask(line):
    p.stdin.write(line + '\n'); p.stdin.flush()
    return p.stdout.readline().strip()
failures = 0
def check(name, got, want):
    global failures
    if got != want:
        failures += 1
        print('FAIL', name, got, want)

# IEEE 802.11i-2004 H.4.1
check('psk vector', ask('psk ' + b'password'.hex() + ' ' + b'IEEE'.hex()), 'f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e')
rng = random.Random(7)
def rb(n): return bytes(rng.randrange(256) for _ in range(n))
for i in range(5):
    pw = ''.join(rng.choice('abcdefghij0123456789') for _ in range(8 + i * 7)).encode(); ssid = rb(1 + i * 7)
    check('psk', ask(f'psk {pw.hex()} {ssid.hex()}'), hashlib.pbkdf2_hmac('sha1', pw, ssid, 4096, 32).hex())
for n in (0, 1, 55, 56, 63, 64, 65, 200):
    k, m = rb(1 + n % 90), rb(n)
    check('hmac', ask(f'hmac {k.hex()} {m.hex() or "00"[:0]}') if n else ask(f'hmac {k.hex()} '), hmac.new(k, m, hashlib.sha1).hexdigest())
def prf(key, label, data, n):
    out = b''
    i = 0
    while len(out) < n:
        out += hmac.new(key, label + b'\0' + data + bytes([i]), hashlib.sha1).digest(); i += 1
    return out[:n]
for _ in range(5):
    pmk, aa, spa, an, sn = rb(32), rb(6), rb(6), rb(32), rb(32)
    data = min(aa, spa) + max(aa, spa) + min(an, sn) + max(an, sn)
    check('ptk', ask(f'ptk {pmk.hex()} {(aa + spa).hex()} {an.hex()} {sn.hex()}'), prf(pmk, b'Pairwise key expansion', data, 48).hex())
for _ in range(20):
    k, b = rb(16), rb(16)
    e = Cipher(algorithms.AES(k), modes.ECB()).encryptor().update(b)
    check('aes', ask(f'aes {k.hex()} {b.hex()}'), (e + b).hex())
for n in (16, 24, 32, 40):
    kek, key = rb(16), rb(n)
    w = aes_key_wrap(kek, key)
    check('unwrap', ask(f'unwrap {kek.hex()} {w.hex()}'), key.hex())
    bad = bytearray(w); bad[3] ^= 1
    check('unwrap bad', ask(f'unwrap {kek.hex()} {bytes(bad).hex()}'), 'bad')
def ccmp_ref(tk, h, pn, data):
    fc = h[0] | h[1] << 8
    is_data = (fc & 0x0c) == 0x08
    qos = is_data and fc & 0x80
    a4 = (fc & 0x300) == 0x300
    b0 = h[0] & 0x8f if is_data else h[0]
    b1 = (h[1] & 0xc7) | 0x40
    if qos: b1 &= 0x7f
    aad = bytes([b0, b1]) + h[4:22] + bytes([h[22] & 0x0f, 0])
    at = 24
    if a4: aad += h[24:30]; at = 30
    tid = 0
    if qos: tid = h[at] & 0x0f; aad += bytes([tid, 0])
    nonce = bytes([tid]) + h[10:16] + pn.to_bytes(6, 'big')
    return AESCCM(tk, tag_length=8).encrypt(nonce, data, aad)
for case in range(12):
    tk = rb(16)
    qos = case % 2
    fc = bytes([0x88 if qos else 0x08, (0x41 if case != 7 else 0x01) | (0x08 if case % 3 == 0 else 0) | (0x02 if case == 5 else 0) | (0x80 if case in (3, 6) else 0)])
    h = fc + rb(2) + rb(18) + rb(2)
    if case == 5: h += rb(6)
    if qos: h += bytes([rng.randrange(256), rng.randrange(256)])
    pn = rng.randrange(1 << 48)
    data = rb([0, 1, 15, 16, 17, 100, 1500][case % 7])
    got = ask(f'ccmp {tk.hex()} {h.hex()} {pn:012x} {data.hex()}')
    check('ccmp', got, 'roundtrip tamper-detected ' + ccmp_ref(tk, h, pn, data).hex())
p.stdin.close()
print('Wi-Fi crypto: PBKDF2 (802.11i vector), HMAC-SHA1, PRF/PTK, AES, key unwrap, CCMP ' + ('passed' if not failures else f'FAILED ({failures})'))
sys.exit(1 if failures else 0)
