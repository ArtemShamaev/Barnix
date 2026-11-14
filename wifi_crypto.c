/* WPA2 cryptography (see wifi_crypto.h). Written from FIPS 180-4 (SHA-1),
 * RFC 2104/8018 (HMAC, PBKDF2), FIPS 197 (AES), RFC 3394 (key wrap),
 * RFC 3610 and IEEE 802.11-2020 12.5.3 (CCM/CCMP) and 12.7.1 (PRF, PTK). */
#include "wifi_crypto.h"

static void copy(void *to, const void *from, unsigned int n) { uint8_t *d = to; const uint8_t *s = from; while (n--) *d++ = *s++; }
static void zero(void *to, unsigned int n) { uint8_t *d = to; while (n--) *d++ = 0; }
static uint32_t rol(uint32_t v, int n) { return v << n | v >> (32 - n); }

/* ------------------------------------------------------------- SHA-1 */
static void sha1_block(WifiSha1 *s, const uint8_t *p)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
void wifi_sha1_init(WifiSha1 *s)
{
    s->h[0] = 0x67452301; s->h[1] = 0xefcdab89; s->h[2] = 0x98badcfe; s->h[3] = 0x10325476; s->h[4] = 0xc3d2e1f0;
    s->used = 0; s->bytes = 0;
}
void wifi_sha1_update(WifiSha1 *s, const void *data, unsigned int length)
{
    const uint8_t *p = data;
    s->bytes += length;
    while (length) {
        unsigned int n = 64 - s->used;
        if (n > length) n = length;
        copy(s->block + s->used, p, n);
        s->used += n; p += n; length -= n;
        if (s->used == 64) { sha1_block(s, s->block); s->used = 0; }
    }
}
void wifi_sha1_final(WifiSha1 *s, uint8_t out[20])
{
    uint64_t bits = s->bytes * 8;
    uint8_t pad = 0x80;
    wifi_sha1_update(s, &pad, 1);
    pad = 0;
    while (s->used != 56) wifi_sha1_update(s, &pad, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    wifi_sha1_update(s, length, 8);
    for (int i = 0; i < 5; i++) { out[4 * i] = (uint8_t)(s->h[i] >> 24); out[4 * i + 1] = (uint8_t)(s->h[i] >> 16); out[4 * i + 2] = (uint8_t)(s->h[i] >> 8); out[4 * i + 3] = (uint8_t)s->h[i]; }
}
/* HMAC with the key's pads hashed once, for the 8192 rounds of PBKDF2. */
typedef struct { WifiSha1 inner, outer; } Hmac;
static void hmac_init(Hmac *h, const uint8_t *key, unsigned int key_length)
{
    uint8_t k[64], pad[64], digest[20];
    zero(k, 64);
    if (key_length > 64) { WifiSha1 s; wifi_sha1_init(&s); wifi_sha1_update(&s, key, key_length); wifi_sha1_final(&s, digest); copy(k, digest, 20); }
    else copy(k, key, key_length);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    wifi_sha1_init(&h->inner); wifi_sha1_update(&h->inner, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    wifi_sha1_init(&h->outer); wifi_sha1_update(&h->outer, pad, 64);
}
static void hmac_run(const Hmac *h, const uint8_t *data, unsigned int length, uint8_t out[20])
{
    WifiSha1 s = h->inner;
    uint8_t digest[20];
    wifi_sha1_update(&s, data, length); wifi_sha1_final(&s, digest);
    s = h->outer;
    wifi_sha1_update(&s, digest, 20); wifi_sha1_final(&s, out);
}
void wifi_hmac_sha1(const uint8_t *key, unsigned int key_length, const uint8_t *data, unsigned int length, uint8_t out[20])
{
    Hmac h;
    hmac_init(&h, key, key_length);
    hmac_run(&h, data, length, out);
}
void wifi_psk(const char *passphrase, const uint8_t *ssid, unsigned int ssid_length, uint8_t pmk[32])
{
    unsigned int pass_length = 0;
    while (passphrase[pass_length]) pass_length++;
    Hmac h;
    hmac_init(&h, (const uint8_t *)passphrase, pass_length);
    for (uint32_t block = 1; block <= 2; block++) {
        uint8_t salt[36], u[20], t[20];
        if (ssid_length > 32) ssid_length = 32;
        copy(salt, ssid, ssid_length);
        salt[ssid_length] = (uint8_t)(block >> 24); salt[ssid_length + 1] = (uint8_t)(block >> 16);
        salt[ssid_length + 2] = (uint8_t)(block >> 8); salt[ssid_length + 3] = (uint8_t)block;
        hmac_run(&h, salt, ssid_length + 4, u);
        copy(t, u, 20);
        for (int i = 1; i < 4096; i++) {
            hmac_run(&h, u, 20, u);
            for (int k = 0; k < 20; k++) t[k] ^= u[k];
        }
        copy(pmk + (block - 1) * 20, t, block == 1 ? 20 : 12);
    }
}
void wifi_prf(const uint8_t *key, unsigned int key_length, const char *label,
              const uint8_t *data, unsigned int data_length, uint8_t *out, unsigned int out_length)
{
    uint8_t input[160], digest[20];
    unsigned int label_length = 0;
    while (label[label_length]) label_length++;
    if (label_length + 1 + data_length + 1 > sizeof(input)) return;
    copy(input, label, label_length);
    input[label_length] = 0;
    copy(input + label_length + 1, data, data_length);
    unsigned int n = label_length + 1 + data_length;
    for (uint8_t i = 0; out_length; i++) {
        input[n] = i;
        wifi_hmac_sha1(key, key_length, input, n + 1, digest);
        unsigned int take = out_length < 20 ? out_length : 20;
        copy(out, digest, take);
        out += take; out_length -= take;
    }
}
static int less(const uint8_t *a, const uint8_t *b, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i];
    return 0;
}
void wifi_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
              const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48])
{
    uint8_t data[76];
    int a = less(aa, spa, 6), n = less(anonce, snonce, 32);
    copy(data, a ? aa : spa, 6); copy(data + 6, a ? spa : aa, 6);
    copy(data + 12, n ? anonce : snonce, 32); copy(data + 44, n ? snonce : anonce, 32);
    wifi_prf(pmk, 32, "Pairwise key expansion", data, sizeof(data), ptk, 48);
}

/* ------------------------------------------------------------- AES-128 */
static uint8_t sbox[256], inv_sbox[256];
static int tables_ready;
static uint8_t xtime(uint8_t x) { return (uint8_t)(x << 1 ^ (x & 0x80 ? 0x1b : 0)); }
static uint8_t mul(uint8_t a, uint8_t b) { uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; } return r; }
/* The S-box from its definition: inverse in GF(2^8), then the affine map. */
static void make_tables(void)
{
    uint8_t p = 1, q = 1;
    do {
        p = p ^ xtime(p);                       /* multiply by 3 */
        q ^= q << 1; q ^= q << 2; q ^= q << 4;  /* divide by 3 */
        if (q & 0x80) q ^= 0x09;
        uint8_t x = q ^ (uint8_t)(q << 1 | q >> 7) ^ (uint8_t)(q << 2 | q >> 6) ^ (uint8_t)(q << 3 | q >> 5) ^ (uint8_t)(q << 4 | q >> 4);
        sbox[p] = x ^ 0x63;
    } while (p != 1);
    sbox[0] = 0x63;
    for (int i = 0; i < 256; i++) inv_sbox[sbox[i]] = (uint8_t)i;
    tables_ready = 1;
}
void wifi_aes_key(WifiAes *aes, const uint8_t key[16])
{
    if (!tables_ready) make_tables();
    uint32_t *w = aes->enc;
    for (int i = 0; i < 4; i++) w[i] = (uint32_t)key[4 * i] << 24 | (uint32_t)key[4 * i + 1] << 16 | (uint32_t)key[4 * i + 2] << 8 | key[4 * i + 3];
    uint8_t rcon = 1;
    for (int i = 4; i < 44; i++) {
        uint32_t t = w[i - 1];
        if (i % 4 == 0) {
            t = (uint32_t)sbox[t >> 16 & 255] << 24 | (uint32_t)sbox[t >> 8 & 255] << 16 | (uint32_t)sbox[t & 255] << 8 | sbox[t >> 24];
            t ^= (uint32_t)rcon << 24;
            rcon = xtime(rcon);
        }
        w[i] = w[i - 4] ^ t;
    }
    for (int i = 0; i < 44; i++) aes->dec[i] = aes->enc[i];   /* the plain inverse cipher uses the same schedule */
}
static void add_round_key(uint8_t s[16], const uint32_t *k)
{
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) s[4 * c + r] ^= (uint8_t)(k[c] >> (24 - 8 * r));
}
void wifi_aes_encrypt(const WifiAes *aes, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];
    copy(s, in, 16);
    add_round_key(s, aes->enc);
    for (int round = 1; round <= 10; round++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) t[4 * c + r] = s[4 * ((c + r) % 4) + r];   /* ShiftRows */
        if (round < 10)
            for (int c = 0; c < 4; c++) {   /* MixColumns */
                uint8_t *x = t + 4 * c, a0 = x[0], a1 = x[1], a2 = x[2], a3 = x[3];
                s[4 * c] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
                s[4 * c + 1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
                s[4 * c + 2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
                s[4 * c + 3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
            }
        else copy(s, t, 16);
        add_round_key(s, aes->enc + 4 * round);
    }
    copy(out, s, 16);
}
void wifi_aes_decrypt(const WifiAes *aes, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];
    copy(s, in, 16);
    add_round_key(s, aes->dec + 40);
    for (int round = 9; round >= 0; round--) {
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) t[4 * ((c + r) % 4) + r] = s[4 * c + r];   /* InvShiftRows */
        for (int i = 0; i < 16; i++) s[i] = inv_sbox[t[i]];
        add_round_key(s, aes->dec + 4 * round);
        if (round)
            for (int c = 0; c < 4; c++) {   /* InvMixColumns */
                uint8_t *x = s + 4 * c, a0 = x[0], a1 = x[1], a2 = x[2], a3 = x[3];
                x[0] = mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9);
                x[1] = mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13);
                x[2] = mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11);
                x[3] = mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14);
            }
    }
    copy(out, s, 16);
}
int wifi_aes_unwrap(const uint8_t kek[16], const uint8_t *in, unsigned int length, uint8_t *out)
{
    if (length < 24 || length % 8 || length > 8 + 256) return -1;
    WifiAes aes;
    wifi_aes_key(&aes, kek);
    unsigned int n = length / 8 - 1;
    uint8_t a[8], b[16];
    copy(a, in, 8);
    copy(out, in + 8, n * 8);
    for (int j = 5; j >= 0; j--)
        for (unsigned int i = n; i >= 1; i--) {
            uint32_t t = n * (unsigned int)j + i;
            copy(b, a, 8);
            b[4] ^= (uint8_t)(t >> 24); b[5] ^= (uint8_t)(t >> 16); b[6] ^= (uint8_t)(t >> 8); b[7] ^= (uint8_t)t;
            copy(b + 8, out + 8 * (i - 1), 8);
            wifi_aes_decrypt(&aes, b, b);
            copy(a, b, 8);
            copy(out + 8 * (i - 1), b + 8, 8);
        }
    for (int i = 0; i < 8; i++) if (a[i] != 0xa6) return -1;
    return 0;
}

/* ------------------------------------------------------------- CCMP */
/* The CCM nonce and AAD come from the masked MPDU header (802.11-2020 12.5.3.3). */
static unsigned int ccmp_aad(const uint8_t *h, unsigned int header_length, uint8_t aad[32], uint8_t nonce[13], uint64_t pn)
{
    unsigned int fc = h[0] | h[1] << 8;
    int qos = (fc & 0x0c) == 0x08 && (fc & 0x80);
    int a4 = (fc & 0x300) == 0x300;
    unsigned int n = 0;
    aad[n++] = h[0] & 0x8f;                    /* subtype bits 4-6 masked in data frames */
    if ((fc & 0x0c) != 0x08) aad[0] = h[0];
    aad[n++] = (uint8_t)((h[1] & 0xc7) | 0x40);   /* retry, power management, more data cleared; protected set */
    copy(aad + n, h + 4, 18); n += 18;         /* A1 A2 A3 */
    aad[n++] = h[22] & 0x0f; aad[n++] = 0;     /* sequence control: fragment number only */
    unsigned int at = 24;
    if (a4) { copy(aad + n, h + 24, 6); n += 6; at += 6; }
    uint8_t tid = 0;
    if (qos) { tid = h[at] & 0x0f; aad[n++] = h[at] & 0x0f; aad[n++] = 0; aad[1] &= 0x7f; }   /* QoS: Order masked */
    (void)header_length;
    nonce[0] = tid;
    copy(nonce + 1, h + 10, 6);               /* A2 */
    for (int i = 0; i < 6; i++) nonce[7 + i] = (uint8_t)(pn >> (40 - 8 * i));
    return n;
}
/* CBC-MAC over B0 | AAD | data, M = 8, L = 2. */
static void ccm_mac(const WifiAes *k, const uint8_t nonce[13], const uint8_t *aad, unsigned int aad_length,
                    const uint8_t *data, unsigned int length, uint8_t tag[16])
{
    uint8_t x[16], b[16];
    b[0] = 0x59;   /* Adata, M = 8, L = 2 */
    copy(b + 1, nonce, 13);
    b[14] = (uint8_t)(length >> 8); b[15] = (uint8_t)length;
    wifi_aes_encrypt(k, b, x);
    zero(b, 16);
    b[0] = (uint8_t)(aad_length >> 8); b[1] = (uint8_t)aad_length;
    unsigned int n = 2;
    for (unsigned int i = 0; i < aad_length; i++) {
        b[n++] = aad[i];
        if (n == 16) { for (int j = 0; j < 16; j++) x[j] ^= b[j]; wifi_aes_encrypt(k, x, x); zero(b, 16); n = 0; }
    }
    if (n) { for (int j = 0; j < 16; j++) x[j] ^= b[j]; wifi_aes_encrypt(k, x, x); }
    for (unsigned int i = 0; i < length; i += 16) {
        for (unsigned int j = 0; j < 16 && i + j < length; j++) x[j] ^= data[i + j];
        wifi_aes_encrypt(k, x, x);
    }
    copy(tag, x, 16);
}
static void ccm_ctr(const WifiAes *k, const uint8_t nonce[13], unsigned int counter, uint8_t out[16])
{
    uint8_t a[16];
    a[0] = 0x01;   /* L - 1 */
    copy(a + 1, nonce, 13);
    a[14] = (uint8_t)(counter >> 8); a[15] = (uint8_t)counter;
    wifi_aes_encrypt(k, a, out);
}
void wifi_ccmp_encrypt(const WifiAes *tk, const uint8_t *header, unsigned int header_length,
                       uint64_t pn, const uint8_t *data, unsigned int length, uint8_t *out)
{
    uint8_t aad[32], nonce[13], tag[16], s[16];
    unsigned int aad_length = ccmp_aad(header, header_length, aad, nonce, pn);
    ccm_mac(tk, nonce, aad, aad_length, data, length, tag);
    for (unsigned int i = 0; i < length; i += 16) {
        ccm_ctr(tk, nonce, i / 16 + 1, s);
        for (unsigned int j = 0; j < 16 && i + j < length; j++) out[i + j] = data[i + j] ^ s[j];
    }
    ccm_ctr(tk, nonce, 0, s);
    for (int j = 0; j < 8; j++) out[length + j] = tag[j] ^ s[j];
}
int wifi_ccmp_decrypt(const WifiAes *tk, const uint8_t *header, unsigned int header_length,
                      uint64_t pn, const uint8_t *data, unsigned int length, uint8_t *out)
{
    if (length < 8) return -1;
    length -= 8;
    uint8_t aad[32], nonce[13], tag[16], s[16];
    unsigned int aad_length = ccmp_aad(header, header_length, aad, nonce, pn);
    for (unsigned int i = 0; i < length; i += 16) {
        ccm_ctr(tk, nonce, i / 16 + 1, s);
        for (unsigned int j = 0; j < 16 && i + j < length; j++) out[i + j] = data[i + j] ^ s[j];
    }
    ccm_mac(tk, nonce, aad, aad_length, out, length, tag);
    ccm_ctr(tk, nonce, 0, s);
    uint8_t diff = 0;
    for (int j = 0; j < 8; j++) diff |= (uint8_t)(tag[j] ^ s[j] ^ data[length + j]);
    return diff ? -1 : 0;
}
