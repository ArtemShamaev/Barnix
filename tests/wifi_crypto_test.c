/* Prints wifi_crypto results for tests/wifi_crypto_test.py to compare with
 * Python's hashlib and cryptography. Input lines: an operation and hex fields. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_crypto.h"

static int unhex(const char *s, uint8_t *out) { int n = 0; while (s[0] && s[1] && s[0] != '\n') { sscanf(s, "%2hhx", &out[n++]); s += 2; } return n; }
static void hex(const uint8_t *p, int n) { for (int i = 0; i < n; i++) printf("%02x", p[i]); printf("\n"); }
int main(void)
{
    char line[8192], op[16], a[4096], b[4096], c[4096], d[4096];
    static uint8_t x[2048], y[2048], z[2048], w[2048], out[4096];
    while (fgets(line, sizeof(line), stdin)) {
        a[0] = b[0] = c[0] = d[0] = 0;
        if (sscanf(line, "%15s %4095s %4095s %4095s %4095s", op, a, b, c, d) < 1) continue;
        if (!strcmp(op, "psk")) { int n = unhex(b, y); x[unhex(a, x)] = 0; wifi_psk((char *)x, y, n, out); hex(out, 32); }
        else if (!strcmp(op, "hmac")) { int k = unhex(a, x), n = unhex(b, y); wifi_hmac_sha1(x, k, y, n, out); hex(out, 20); }
        else if (!strcmp(op, "ptk")) { unhex(a, x); unhex(b, y); unhex(c, z); unhex(d, w);
            /* a: pmk, b: aa|spa, c: anonce, d: snonce */
            wifi_ptk(x, y, y + 6, z, w, out); hex(out, 48); }
        else if (!strcmp(op, "aes")) { WifiAes k; unhex(a, x); unhex(b, y); wifi_aes_key(&k, x); wifi_aes_encrypt(&k, y, out); wifi_aes_decrypt(&k, out, out + 16); hex(out, 32); }
        else if (!strcmp(op, "unwrap")) { unhex(a, x); int n = unhex(b, y); int r = wifi_aes_unwrap(x, y, n, out); if (r) printf("bad\n"); else hex(out, n - 8); }
        else if (!strcmp(op, "ccmp")) {
            /* a: tk, b: header, c: pn (hex 12), d: plaintext */
            WifiAes k; unhex(a, x); int h = unhex(b, y); uint64_t pn = strtoull(c, 0, 16); int n = unhex(d, z);
            wifi_aes_key(&k, x);
            wifi_ccmp_encrypt(&k, y, h, pn, z, n, out);
            int ok = !wifi_ccmp_decrypt(&k, y, h, pn, out, n + 8, w) && !memcmp(w, z, n);
            out[0] ^= 1; int tamper = wifi_ccmp_decrypt(&k, y, h, pn, out, n + 8, w); out[0] ^= 1;
            printf("%s %s ", ok ? "roundtrip" : "BROKEN", tamper ? "tamper-detected" : "TAMPER-MISSED"); hex(out, n + 8);
        }
        fflush(stdout);
    }
    return 0;
}
