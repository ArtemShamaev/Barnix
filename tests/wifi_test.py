"""Barnix Wi-Fi against the virtual access point, without QEMU.

tests/wifi_test (wifi.c + net_vwifi.c on the host) plays the guest; this
script stands in for QEMU's user network on the access point's uplink: it
expects the station's decrypted broadcast and answers with a unicast frame,
which the access point encrypts back. Also checks a wrong password and an
unknown network name."""
import os
import socket
import subprocess
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'scripts'))
from virtual_wifi import VirtualAccessPoint, SSID, PASSWORD  # noqa: E402

binary = sys.argv[1]
failures = 0


def check(name, condition, detail=''):
    global failures
    if not condition:
        failures += 1
        print('FAIL', name, detail)


def run(ssid, password, answer=False):
    ap = VirtualAccessPoint(log=lambda text: None)
    uplink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    uplink.bind(('127.0.0.1', ap.qemu_uplink))
    uplink.settimeout(0.2)
    seen = []
    stop = threading.Event()

    def slirp():
        while not stop.is_set():
            try:
                frame = uplink.recv(4096)
            except socket.timeout:
                continue
            seen.append(frame)
            if answer and frame[12:14] == b'\x08\x00':
                reply = frame[6:12] + bytes.fromhex('525400123502') + b'\x08\x00' + b'hello from qemu\0'
                uplink.sendto(reply, ap.uplink.getsockname())

    thread = threading.Thread(target=slirp, daemon=True)
    thread.start()
    ap.start()
    try:
        out = subprocess.run([binary, str(ap.qemu_radio), str(ap.radio.getsockname()[1]), ssid, password],
                             capture_output=True, text=True, timeout=30).stdout
    finally:
        stop.set()
        thread.join()
        ap.stop()
        uplink.close()
    return out, seen


out, seen = run(SSID, PASSWORD, answer=True)
check('adapter', 'adapter Barnix virtual Wi-Fi (QEMU)' in out, out)
check('scan', f'network {SSID} WPA2' in out, out)
check('connect', 'connect 0' in out, out)
check('uplink got the decrypted frame', any(f[14:31] == b'hello from barnix' and f[:6] == b'\xff' * 6 for f in seen), seen)
check('station got the encrypted answer', 'received 0800 hello from qemu' in out, out)
check('link', 'link 1' in out, out)

out, seen = run(SSID, 'not-the-password')
check('wrong password', 'connect -10' in out, out)
check('nothing leaks with a wrong password', not seen, seen)

out, _ = run('No-Such-Network', PASSWORD)
check('unknown network', 'connect -9' in out, out)

if failures:
    sys.exit(f'{failures} Wi-Fi test(s) failed')
print('Wi-Fi: scan, WPA2 4-way handshake, CCMP both ways, wrong password, unknown network passed')
