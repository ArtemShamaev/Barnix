#!/usr/bin/env python3
"""A Wi-Fi network for Barnix under QEMU.

QEMU has no Wi-Fi card models, so Barnix gets a paravirtual one: an RTL8139
whose MAC starts with 02:57:49:46:49 ("WIFI") carries raw 802.11 frames
(Ethernet type 0x88b5, one signal byte, the frame without FCS) to this
process over a UDP socket. Here a WPA2-Personal access point answers: it
beacons, authenticates, associates, runs the 4-way handshake, encrypts with
CCMP and bridges the decrypted traffic to a QEMU user network (DHCP, DNS and
NAT to the Internet) through a second UDP socket and a QEMU hub.

    python3 scripts/virtual_wifi.py [--ssid S] [--password P] -- qemu-system-i386 ...

starts the access point, runs the command with the network options appended
and stops when it exits. Setup/run.py imports VirtualAccessPoint directly.
"""
import argparse
import hashlib
import hmac
import os
import random
import selectors
import socket
import struct
import subprocess
import sys
import threading
import time

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM
    from cryptography.hazmat.primitives.keywrap import aes_key_wrap
except ImportError:   # main() then runs QEMU without Wi-Fi
    AESCCM = aes_key_wrap = None

SSID = 'Quemu-Password-root'
PASSWORD = 'root'
CARD_MAC = '02:57:49:46:49:01'
ETHERTYPE = 0x88b5
CHANNEL = 6
BROADCAST = b'\xff' * 6
RATES = bytes([0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24])
EXT_RATES = bytes([0x30, 0x48, 0x60, 0x6c])
RSN = bytes([1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0, 0])
SNAP = b'\xaa\xaa\x03\x00\x00\x00'


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def element(tag, body):
    return bytes([tag, len(body)]) + body


def elements(body):
    found, at = {}, 0
    while at + 2 <= len(body):
        tag, size = body[at], body[at + 1]
        if at + 2 + size > len(body):
            break
        found.setdefault(tag, body[at + 2:at + 2 + size])
        at += 2 + size
    return found


def prf(key, label, data, length):
    out, i = b'', 0
    while len(out) < length:
        out += hmac.new(key, label + b'\0' + data + bytes([i]), hashlib.sha1).digest()
        i += 1
    return out[:length]


def ccmp_aad_nonce(header, pn):
    """802.11-2020 12.5.3.3: masked frame control, A1-A3, fragment number."""
    fc0, fc1 = header[0], header[1]
    data = (fc0 & 0x0c) == 0x08
    qos = data and fc0 & 0x80
    aad = bytes([fc0 & 0x8f if data else fc0, (fc1 & 0xc7) | 0x40]) + header[4:22] + bytes([header[22] & 0x0f, 0])
    tid = 0
    if qos:
        tid = header[24] & 0x0f
        aad = aad[:1] + bytes([aad[1] & 0x7f]) + aad[2:] + bytes([tid, 0])
    nonce = bytes([tid]) + header[10:16] + pn.to_bytes(6, 'big')
    return aad, nonce


class VirtualAccessPoint:
    def __init__(self, ssid=SSID, password=PASSWORD, card_mac=CARD_MAC, log=None):
        self.ssid = ssid.encode()
        self.pmk = hashlib.pbkdf2_hmac('sha1', password.encode(), self.ssid, 4096, 32)
        self.card_mac = card_mac
        self.bssid = bytes.fromhex('024241500001')
        self.log = log or (lambda text: None)
        self.radio = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.radio.bind(('127.0.0.1', 0))
        self.uplink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.uplink.bind(('127.0.0.1', 0))
        self.qemu_radio, self.qemu_uplink = free_port(), free_port()
        self.gtk = os.urandom(16)
        self.gtk_pn = 0
        self.sequence = 0
        self.station = None
        self.stopping = threading.Event()
        self.thread = None

    # ------------------------------------------------------------ QEMU side
    def qemu_options(self, hub=7):
        def dgram(name, local, remote):
            return ['-netdev', f'dgram,id={name},local.type=inet,local.host=127.0.0.1,local.port={local},'
                    f'remote.type=inet,remote.host=127.0.0.1,remote.port={remote}']
        return (dgram('wifiradio', self.qemu_radio, self.radio.getsockname()[1]) +
                ['-device', f'rtl8139,netdev=wifiradio,mac={self.card_mac}',
                 '-netdev', 'user,id=wifiwan,net=192.168.77.0/24'] +
                dgram('wifiup', self.qemu_uplink, self.uplink.getsockname()[1]) +
                ['-netdev', f'hubport,id=wifihub0,hubid={hub},netdev=wifiwan',
                 '-netdev', f'hubport,id=wifihub1,hubid={hub},netdev=wifiup'])

    def start(self):
        self.thread = threading.Thread(target=self.run, name='virtual-wifi', daemon=True)
        self.thread.start()
        return self

    def stop(self):
        self.stopping.set()
        if self.thread:
            self.thread.join(2)
        self.radio.close()
        self.uplink.close()

    def run(self):
        selector = selectors.DefaultSelector()
        selector.register(self.radio, selectors.EVENT_READ, self.radio_input)
        selector.register(self.uplink, selectors.EVENT_READ, self.uplink_input)
        next_beacon = 0.0
        while not self.stopping.is_set():
            now = time.monotonic()
            if now >= next_beacon:
                self.send_management(8, BROADCAST, self.beacon_body())
                next_beacon = now + 0.5
            self.handshake_timer(now)
            for key, _ in selector.select(0.1):
                try:
                    data = key.fileobj.recv(4096)
                except OSError:
                    continue
                try:
                    key.data(data)
                except Exception as error:   # a malformed frame must not stop the network
                    self.log(f'dropped a frame: {error!r}')

    # ------------------------------------------------------------ radio
    def air(self, frame, destination=BROADCAST):
        signal = (-42 - random.randrange(6)) & 0xff
        ethernet = destination + self.bssid + struct.pack('>H', ETHERTYPE) + bytes([signal]) + frame
        ethernet += b'\0' * (60 - len(ethernet))   # the minimum Ethernet frame: QEMU's UDP backend does not pad
        try:
            self.radio.sendto(ethernet, ('127.0.0.1', self.qemu_radio))
        except OSError:
            pass

    def header(self, fc0, fc1, a1, a2, a3):
        self.sequence = (self.sequence + 1) & 0xfff
        return bytes([fc0, fc1, 0, 0]) + a1 + a2 + a3 + struct.pack('<H', self.sequence << 4)

    def send_management(self, subtype, destination, body):
        self.air(self.header(subtype << 4, 0, destination, self.bssid, self.bssid) + body, destination)

    def beacon_body(self):
        return (struct.pack('<QHH', int(time.monotonic() * 1e6), 100, 0x0431) + element(0, self.ssid) +
                element(1, RATES) + element(3, bytes([CHANNEL])) + element(48, RSN) + element(50, EXT_RATES))

    def radio_input(self, packet):
        if len(packet) < 15 + 24 or struct.unpack('>H', packet[12:14])[0] != ETHERTYPE:
            return
        frame = packet[15:]
        fc0, fc1 = frame[0], frame[1]
        kind, subtype = fc0 >> 2 & 3, fc0 >> 4
        a1, a2 = frame[4:10], frame[10:16]
        if kind == 0:
            self.management(subtype, a1, a2, frame[24:])
        elif kind == 2 and fc1 & 3 == 1 and a1 == self.bssid and self.station and self.station['mac'] == a2:
            self.station_data(frame)

    def management(self, subtype, a1, source, body):
        if subtype == 4:   # probe request
            ssid = elements(body).get(0, b'')
            if ssid in (b'', self.ssid):
                self.send_management(5, source, self.beacon_body())
            return
        if a1 != self.bssid:
            return
        if subtype == 11 and len(body) >= 6:   # authentication
            algorithm, sequence = struct.unpack('<HH', body[:4])
            if sequence == 1:
                status = 0 if algorithm == 0 else 13
                self.send_management(11, source, struct.pack('<HHH', algorithm, 2, status))
                if status == 0:
                    self.station = {'mac': source, 'state': 'authenticated'}
                    self.log(f'{source.hex(":")} authenticated')
        elif subtype in (0, 2) and len(body) >= 4 + 2 * (subtype == 2):   # (re)association request
            ies = elements(body[4 + 6 * (subtype == 2):])
            status = 0
            if not self.station or self.station['mac'] != source:
                status = 1
            elif ies.get(0) != self.ssid:
                status = 1
            elif not ies.get(48) or b'\x00\x0f\xac\x02' not in ies[48] or b'\x00\x0f\xac\x04' not in ies[48]:
                status = 43   # invalid AKMP: only WPA2-PSK with CCMP
            response = struct.pack('<HHH', 0x0431, status, 0xc001) + element(1, RATES) + element(50, EXT_RATES)
            self.send_management(subtype + 1, source, response)
            if status == 0:
                self.station.update(state='handshake', rsn=bytes([48, len(ies[48])]) + ies[48],
                                    anonce=os.urandom(32), replay=0, sent=0.0, tries=0, tk=None, rx_pn=-1, tx_pn=0)
                self.log(f'{source.hex(":")} associated, starting the 4-way handshake')
                self.send_eapol_1()
        elif subtype in (10, 12):   # disassociation, deauthentication
            if self.station and self.station['mac'] == source:
                self.log(f'{source.hex(":")} left')
                self.station = None

    # ------------------------------------------------------------ WPA2 4-way handshake
    def eapol_key(self, info, nonce, key_data=b'', rsc=b'\0' * 8, kck=None):
        s = self.station
        s['replay'] += 1
        body = (struct.pack('>BHH', 2, info, 16) + struct.pack('>Q', s['replay']) + nonce + b'\0' * 16 + rsc +
                b'\0' * 8 + b'\0' * 16 + struct.pack('>H', len(key_data)) + key_data)
        frame = struct.pack('>BBH', 2, 3, len(body)) + body
        if kck:
            mic = hmac.new(kck, frame, hashlib.sha1).digest()[:16]
            frame = frame[:81] + mic + frame[97:]
        return frame

    def send_eapol(self, eapol):
        s = self.station
        header = self.header(0x08, 0x02, s['mac'], self.bssid, self.bssid)
        self.air(header + SNAP + b'\x88\x8e' + eapol, s['mac'])
        s['sent'] = time.monotonic()
        s['tries'] += 1

    def send_eapol_1(self):
        s = self.station
        s['message'] = 1
        self.send_eapol(self.eapol_key(0x008a, s['anonce']))

    def send_eapol_3(self):
        s = self.station
        s['message'] = 3
        gtk_kde = bytes([0xdd, 22, 0x00, 0x0f, 0xac, 1, 1, 0]) + self.gtk
        key_data = RSN_IE + gtk_kde
        if len(key_data) % 8 or len(key_data) < 16:
            key_data += b'\xdd' + b'\0' * ((-len(key_data) - 1) % 8)
        wrapped = aes_key_wrap(s['kek'], key_data)
        rsc = (self.gtk_pn).to_bytes(6, 'little') + b'\0\0'
        self.send_eapol(self.eapol_key(0x13ca, s['anonce'], wrapped, rsc, s['kck']))

    def handshake_timer(self, now):
        s = self.station
        if not s or s['state'] != 'handshake' or now - s['sent'] < 1.0:
            return
        if s['tries'] >= 4:
            self.log(f'{s["mac"].hex(":")}: the 4-way handshake timed out (wrong password?)')
            self.send_management(12, s['mac'], struct.pack('<H', 15))
            self.station = None
        elif s['message'] == 1:
            self.send_eapol_1()
        else:
            self.send_eapol_3()

    def eapol_input(self, eapol):
        s = self.station
        if len(eapol) < 99 or eapol[1] != 3 or s['state'] != 'handshake':
            return
        info = struct.unpack('>H', eapol[5:7])[0]
        replay = struct.unpack('>Q', eapol[9:17])[0]
        mic = eapol[81:97]
        if replay != s['replay'] or not info & 0x100:
            return
        if s['message'] == 1:
            snonce = eapol[17:49]
            a, b = sorted([self.bssid, s['mac']])
            n1, n2 = sorted([s['anonce'], snonce])
            ptk = prf(self.pmk, b'Pairwise key expansion', a + b + n1 + n2, 48)
            kck = ptk[:16]
            check = hmac.new(kck, eapol[:81] + b'\0' * 16 + eapol[97:], hashlib.sha1).digest()[:16]
            if not hmac.compare_digest(check, mic):
                self.log(f'{s["mac"].hex(":")}: message 2 has a bad MIC: wrong password')
                self.send_management(12, s['mac'], struct.pack('<H', 15))
                self.station = None
                return
            s.update(kck=kck, kek=ptk[16:32], tk=ptk[32:48], tries=0)
            self.send_eapol_3()
        elif s['message'] == 3:
            check = hmac.new(s['kck'], eapol[:81] + b'\0' * 16 + eapol[97:], hashlib.sha1).digest()[:16]
            if not hmac.compare_digest(check, mic):
                return
            s['state'] = 'connected'
            self.log(f'{s["mac"].hex(":")} connected to {self.ssid.decode()} (WPA2-PSK, CCMP)')

    # ------------------------------------------------------------ data
    def station_data(self, frame):
        s = self.station
        fc0, fc1 = frame[0], frame[1]
        if fc0 & 0x40:   # null data
            return
        header_length = 26 if fc0 & 0x80 else 24
        header, body = frame[:header_length], frame[header_length:]
        if fc1 & 0x40:
            if not s.get('tk') or s['state'] != 'connected' or len(body) < 16:
                return
            pn = body[0] | body[1] << 8 | body[4] << 16 | body[5] << 24 | body[6] << 32 | body[7] << 40
            if pn <= s['rx_pn']:
                return
            aad, nonce = ccmp_aad_nonce(header, pn)
            try:
                body = AESCCM(s['tk'], tag_length=8).decrypt(nonce, body[8:], aad)
            except Exception:
                return
            s['rx_pn'] = pn
        if body[:6] != SNAP or len(body) < 8:
            return
        kind = body[6:8]
        if kind == b'\x88\x8e':
            self.eapol_input(body[8:])
            return
        if s['state'] != 'connected' or not fc1 & 0x40:
            return
        destination = frame[16:22]
        ethernet = destination + s['mac'] + kind + body[8:]
        try:
            self.uplink.sendto(ethernet, ('127.0.0.1', self.qemu_uplink))
        except OSError:
            pass

    def uplink_input(self, ethernet):
        s = self.station
        if not s or s['state'] != 'connected' or len(ethernet) < 14:
            return
        destination, source, kind = ethernet[:6], ethernet[6:12], ethernet[12:14]
        group = destination[0] & 1
        if not group and destination != s['mac']:
            return
        header = bytearray(self.header(0x08, 0x42, destination, self.bssid, source))
        if group:
            self.gtk_pn += 1
            pn, key, key_id = self.gtk_pn, self.gtk, 1
        else:
            s['tx_pn'] += 1
            pn, key, key_id = s['tx_pn'], s['tk'], 0
        aad, nonce = ccmp_aad_nonce(bytes(header), pn)
        p = pn.to_bytes(6, 'little')
        ccmp = bytes([p[0], p[1], 0, 0x20 | key_id << 6, p[2], p[3], p[4], p[5]])
        sealed = AESCCM(key, tag_length=8).encrypt(nonce, SNAP + kind + ethernet[14:], aad)
        self.air(bytes(header) + ccmp + sealed, s['mac'] if not group else BROADCAST)


RSN_IE = bytes([48, len(RSN)]) + RSN


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ssid', default=SSID)
    parser.add_argument('--password', default=PASSWORD)
    parser.add_argument('--quiet', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER, help='QEMU command line after --')
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('expected a QEMU command after --')
    log = (lambda text: None) if args.quiet else (lambda text: print(f'[Wi-Fi] {text}', file=sys.stderr, flush=True))
    if AESCCM is None:
        log('Python "cryptography" is missing (pip install cryptography): starting QEMU without Wi-Fi')
        os.execvp(command[0], command)
    if any('rtl8139' in part for part in command):
        log('warning: the wired card is an RTL8139 too; Barnix drives one per model, so it may see no Wi-Fi')
    ap = VirtualAccessPoint(args.ssid, args.password, log=log).start()
    log(f'access point "{args.ssid}" (WPA2-PSK, password "{args.password}") on channel {CHANNEL}')
    try:
        result = subprocess.run(command + ap.qemu_options()).returncode
    except KeyboardInterrupt:
        result = 130
    finally:
        ap.stop()
    sys.exit(result)


if __name__ == '__main__':
    main()
