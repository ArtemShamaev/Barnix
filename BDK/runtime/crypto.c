/* Barnix Net cryptography: SHA-1/256/384/512, HMAC and HKDF (SHA-256),
 * AES-128-GCM, ChaCha20-Poly1305 and X25519. Straightforward portable code:
 * no tables beyond the AES S-box, no 64-bit division. */
#include "bnet.h"

static uint32_t rol32(uint32_t x, int n) { return x << n | x >> (32 - n); }
static uint32_t ror32(uint32_t x, int n) { return x >> n | x << (32 - n); }
static uint64_t ror64(uint64_t x, int n) { return x >> n | x << (64 - n); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static void put_be64(uint8_t *p, uint64_t v) { put_be32(p, v >> 32); put_be32(p + 4, (uint32_t)v); }
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* ------------------------------------------------------------ SHA-1 */
static void sha1_block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
void sha1_init(Sha1 *s)
{
    static const uint32_t iv[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    memcpy(s->h, iv, sizeof(iv)); s->length = 0; s->used = 0;
}
void sha1_update(Sha1 *s, const void *data, size_t length)
{
    const uint8_t *p = data;
    s->length += length;
    while (length) {
        if (!s->used && length >= 64) { sha1_block(s, p); p += 64; length -= 64; continue; }
        size_t part = 64 - s->used < length ? 64 - s->used : length;
        memcpy(s->block + s->used, p, part); s->used += part; p += part; length -= part;
        if (s->used == 64) { sha1_block(s, s->block); s->used = 0; }
    }
}
void sha1_final(Sha1 *s, uint8_t out[20])
{
    uint64_t bits = s->length << 3;
    uint8_t pad = 0x80, zero = 0, size[8];
    sha1_update(s, &pad, 1);
    while (s->used != 56) sha1_update(s, &zero, 1);
    put_be64(size, bits); sha1_update(s, size, 8);
    for (int i = 0; i < 5; i++) put_be32(out + 4 * i, s->h[i]);
}

/* ------------------------------------------------------------ SHA-256 */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static void sha256_block(Sha256 *s, const uint8_t *p)
{
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->h, sizeof(v));
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = ror32(v[4], 6) ^ ror32(v[4], 11) ^ ror32(v[4], 25);
        uint32_t t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K256[i] + w[i];
        uint32_t s0 = ror32(v[0], 2) ^ ror32(v[0], 13) ^ ror32(v[0], 22);
        uint32_t t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->h[i] += v[i];
}
void sha256_init(Sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof(iv)); s->length = 0; s->used = 0;
}
void sha256_update(Sha256 *s, const void *data, size_t length)
{
    const uint8_t *p = data;
    s->length += length;
    while (length) {
        if (!s->used && length >= 64) { sha256_block(s, p); p += 64; length -= 64; continue; }
        size_t part = 64 - s->used < length ? 64 - s->used : length;
        memcpy(s->block + s->used, p, part); s->used += part; p += part; length -= part;
        if (s->used == 64) { sha256_block(s, s->block); s->used = 0; }
    }
}
void sha256_final(Sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->length << 3;
    uint8_t pad = 0x80, zero = 0, size[8];
    sha256_update(s, &pad, 1);
    while (s->used != 56) sha256_update(s, &zero, 1);
    put_be64(size, bits); sha256_update(s, size, 8);
    for (int i = 0; i < 8; i++) put_be32(out + 4 * i, s->h[i]);
}

/* ------------------------------------------------------------ SHA-512 / 384 */
static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
static void sha512_block(Sha512 *s, const uint8_t *p)
{
    uint64_t w[80], v[8];
    for (int i = 0; i < 16; i++) w[i] = be64(p + 8 * i);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->h, sizeof(v));
    for (int i = 0; i < 80; i++) {
        uint64_t s1 = ror64(v[4], 14) ^ ror64(v[4], 18) ^ ror64(v[4], 41);
        uint64_t t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K512[i] + w[i];
        uint64_t s0 = ror64(v[0], 28) ^ ror64(v[0], 34) ^ ror64(v[0], 39);
        uint64_t t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->h[i] += v[i];
}
void sha512_init(Sha512 *s)
{
    static const uint64_t iv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    memcpy(s->h, iv, sizeof(iv)); s->length = 0; s->used = 0; s->out = 64;
}
void sha384_init(Sha512 *s)
{
    static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                                   0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
    memcpy(s->h, iv, sizeof(iv)); s->length = 0; s->used = 0; s->out = 48;
}
void sha512_update(Sha512 *s, const void *data, size_t length)
{
    const uint8_t *p = data;
    s->length += length;
    while (length) {
        if (!s->used && length >= 128) { sha512_block(s, p); p += 128; length -= 128; continue; }
        size_t part = 128 - s->used < length ? 128 - s->used : length;
        memcpy(s->block + s->used, p, part); s->used += part; p += part; length -= part;
        if (s->used == 128) { sha512_block(s, s->block); s->used = 0; }
    }
}
void sha512_final(Sha512 *s, uint8_t *out)
{
    uint64_t bits = s->length << 3;
    uint8_t pad = 0x80, zero = 0, size[16] = {0};
    sha512_update(s, &pad, 1);
    while (s->used != 112) sha512_update(s, &zero, 1);
    put_be64(size + 8, bits); sha512_update(s, size, 16);
    uint8_t full[64];
    for (int i = 0; i < 8; i++) put_be64(full + 8 * i, s->h[i]);
    memcpy(out, full, s->out);
}
unsigned int hash_digest(int algorithm, const void *data, size_t length, uint8_t *out)
{
    if (algorithm == 1) { Sha1 s; sha1_init(&s); sha1_update(&s, data, length); sha1_final(&s, out); return 20; }
    if (algorithm == 256) { Sha256 s; sha256_init(&s); sha256_update(&s, data, length); sha256_final(&s, out); return 32; }
    Sha512 s;
    if (algorithm == 384) sha384_init(&s); else sha512_init(&s);
    sha512_update(&s, data, length); sha512_final(&s, out);
    return s.out;
}

/* ------------------------------------------------------------ HMAC, HKDF */
void hmac_sha256(const uint8_t *key, size_t key_length, const void *data, size_t length, uint8_t out[32])
{
    uint8_t k[64] = {0}, pad[64], inner[32];
    if (key_length > 64) hash_digest(256, key, key_length, k); else memcpy(k, key, key_length);
    Sha256 s;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&s); sha256_update(&s, pad, 64); sha256_update(&s, data, length); sha256_final(&s, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&s); sha256_update(&s, pad, 64); sha256_update(&s, inner, 32); sha256_final(&s, out);
}
void hkdf_extract(const uint8_t *salt, size_t salt_length, const uint8_t *ikm, size_t ikm_length, uint8_t out[32])
{
    uint8_t zero[32] = {0};
    if (!salt) { salt = zero; salt_length = 32; }
    hmac_sha256(salt, salt_length, ikm, ikm_length, out);
}
void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *context, size_t context_length,
                       uint8_t *out, size_t length)
{
    uint8_t info[2 + 1 + 6 + 64 + 1 + 64], block[32 + sizeof(info) + 1], t[32];
    size_t label_length = strlen(label), n = 0;
    info[n++] = length >> 8; info[n++] = (uint8_t)length;
    info[n++] = (uint8_t)(6 + label_length);
    memcpy(info + n, "tls13 ", 6); n += 6;
    memcpy(info + n, label, label_length); n += label_length;
    info[n++] = (uint8_t)context_length;
    if (context_length) memcpy(info + n, context, context_length);
    n += context_length;
    size_t done = 0, previous = 0;
    for (uint8_t counter = 1; done < length; counter++) {
        memcpy(block, t, previous);
        memcpy(block + previous, info, n);
        block[previous + n] = counter;
        hmac_sha256(secret, 32, block, previous + n + 1, t);
        previous = 32;
        size_t part = length - done < 32 ? length - done : 32;
        memcpy(out + done, t, part); done += part;
    }
}

/* ------------------------------------------------------------ AES-128 */
static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,
    0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,
    0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,
    0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,
    0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,
    0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
void aes128_init(Aes128 *a, const uint8_t key[16])
{
    static const uint8_t rcon[10] = {1, 2, 4, 8, 16, 32, 64, 128, 0x1b, 0x36};
    for (int i = 0; i < 4; i++) a->round[i] = be32(key + 4 * i);
    for (int i = 4; i < 44; i++) {
        uint32_t t = a->round[i - 1];
        if (!(i & 3)) {
            t = (uint32_t)sbox[t >> 16 & 255] << 24 | (uint32_t)sbox[t >> 8 & 255] << 16 |
                (uint32_t)sbox[t & 255] << 8 | sbox[t >> 24];
            t ^= (uint32_t)rcon[i / 4 - 1] << 24;
        }
        a->round[i] = a->round[i - 4] ^ t;
    }
}
static uint8_t xtime(uint8_t x) { return (uint8_t)(x << 1 ^ (x & 0x80 ? 0x1b : 0)); }
void aes128_block(const Aes128 *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];
    for (int c = 0; c < 4; c++) put_be32(s + 4 * c, be32(in + 4 * c) ^ a->round[c]);
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) t[i] = sbox[s[(i + 4 * (i & 3)) & 15]];   /* SubBytes + ShiftRows */
        if (r < 10)
            for (int c = 0; c < 4; c++) {
                uint8_t *col = t + 4 * c, x = col[0] ^ col[1] ^ col[2] ^ col[3], first = col[0];
                col[0] ^= x ^ xtime(col[0] ^ col[1]);
                col[1] ^= x ^ xtime(col[1] ^ col[2]);
                col[2] ^= x ^ xtime(col[2] ^ col[3]);
                col[3] ^= x ^ xtime(col[3] ^ first);
            }
        for (int c = 0; c < 4; c++) put_be32(s + 4 * c, be32(t + 4 * c) ^ a->round[4 * r + c]);
    }
    memcpy(out, s, 16);
}

/* ------------------------------------------------------------ GCM */
typedef struct { uint64_t hi, lo; } Block;
static Block ghash_mul(Block x, Block h)
{
    Block z = {0, 0}, v = h;
    for (int i = 0; i < 128; i++) {
        uint64_t bit = i < 64 ? x.hi >> (63 - i) & 1 : x.lo >> (127 - i) & 1;
        if (bit) { z.hi ^= v.hi; z.lo ^= v.lo; }
        uint64_t carry = v.lo & 1;
        v.lo = v.lo >> 1 | v.hi << 63; v.hi >>= 1;
        if (carry) v.hi ^= 0xe100000000000000ULL;
    }
    return z;
}
static void ghash(Block *y, Block h, const uint8_t *data, size_t length)
{
    while (length) {
        uint8_t block[16] = {0};
        size_t part = length < 16 ? length : 16;
        memcpy(block, data, part);
        y->hi ^= be64(block); y->lo ^= be64(block + 8);
        *y = ghash_mul(*y, h);
        data += part; length -= part;
    }
}
static void gcm(const uint8_t key[16], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                uint8_t *data, size_t length, uint8_t tag[16], int seal)
{
    Aes128 a; aes128_init(&a, key);
    uint8_t zero[16] = {0}, hb[16], counter[16], stream[16];
    aes128_block(&a, zero, hb);
    Block h = {be64(hb), be64(hb + 8)}, y = {0, 0};
    memcpy(counter, nonce, 12); put_be32(counter + 12, 1);
    uint8_t j0[16]; aes128_block(&a, counter, j0);
    ghash(&y, h, aad, aad_length);
    if (!seal) ghash(&y, h, data, length);
    for (size_t at = 0, n = 2; at < length; at += 16, n++) {
        put_be32(counter + 12, (uint32_t)n);
        aes128_block(&a, counter, stream);
        for (size_t i = 0; i < 16 && at + i < length; i++) data[at + i] ^= stream[i];
    }
    if (seal) ghash(&y, h, data, length);
    uint8_t lengths[16];
    put_be64(lengths, (uint64_t)aad_length * 8); put_be64(lengths + 8, (uint64_t)length * 8);
    ghash(&y, h, lengths, 16);
    put_be64(tag, y.hi); put_be64(tag + 8, y.lo);
    for (int i = 0; i < 16; i++) tag[i] ^= j0[i];
}
void aes_gcm_seal(const uint8_t key[16], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                  uint8_t *data, size_t length, uint8_t tag[16])
{ gcm(key, nonce, aad, aad_length, data, length, tag, 1); }
int aes_gcm_open(const uint8_t key[16], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                 uint8_t *data, size_t length, const uint8_t tag[16])
{
    uint8_t expected[16], diff = 0;
    gcm(key, nonce, aad, aad_length, data, length, expected, 0);
    for (int i = 0; i < 16; i++) diff |= expected[i] ^ tag[i];
    return diff ? -1 : 0;
}

/* ------------------------------------------------------------ ChaCha20-Poly1305 */
#define QR(a, b, c, d) a += b; d = rol32(d ^ a, 16); c += d; b = rol32(b ^ c, 12); \
                       a += b; d = rol32(d ^ a, 8); c += d; b = rol32(b ^ c, 7)
static void chacha_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t s[16], x[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = le32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; i++) s[13 + i] = le32(nonce + 4 * i);
    memcpy(x, s, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]); QR(x[1], x[5], x[9], x[13]); QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]); QR(x[2], x[7], x[8], x[13]); QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) put_le32(out + 4 * i, x[i] + s[i]);
}
typedef struct { uint32_t r[5], h[5], pad[4]; } Poly;
static void poly_init(Poly *p, const uint8_t key[32])
{
    p->r[0] = le32(key) & 0x3ffffff;
    p->r[1] = le32(key + 3) >> 2 & 0x3ffff03;
    p->r[2] = le32(key + 6) >> 4 & 0x3ffc0ff;
    p->r[3] = le32(key + 9) >> 6 & 0x3f03fff;
    p->r[4] = le32(key + 12) >> 8 & 0x00fffff;
    for (int i = 0; i < 5; i++) p->h[i] = 0;
    for (int i = 0; i < 4; i++) p->pad[i] = le32(key + 16 + 4 * i);
}
static void poly_block(Poly *p, const uint8_t m[16], uint32_t hibit)
{
    uint32_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = p->h[0] + (le32(m) & 0x3ffffff), h1 = p->h[1] + (le32(m + 3) >> 2 & 0x3ffffff),
             h2 = p->h[2] + (le32(m + 6) >> 4 & 0x3ffffff), h3 = p->h[3] + (le32(m + 9) >> 6 & 0x3ffffff),
             h4 = p->h[4] + (le32(m + 12) >> 8 | hibit);
    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
    uint32_t c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
    d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
    d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
    d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
    d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    p->h[0] = h0; p->h[1] = h1; p->h[2] = h2; p->h[3] = h3; p->h[4] = h4;
}
static void poly_update(Poly *p, const uint8_t *data, size_t length)
{
    while (length >= 16) { poly_block(p, data, 1U << 24); data += 16; length -= 16; }
    if (length) {   /* AEAD pads with zeros to 16 bytes: a full block */
        uint8_t block[16] = {0};
        memcpy(block, data, length);
        poly_block(p, block, 1U << 24);
    }
}
static void poly_final(Poly *p, uint8_t tag[16])
{
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4], c;
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff; h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1U << 26);
    uint32_t mask = (g4 >> 31) - 1;   /* all ones when h >= p */
    h0 = (h0 & ~mask) | (g0 & mask); h1 = (h1 & ~mask) | (g1 & mask); h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask); h4 = (h4 & ~mask) | (g4 & mask);
    h0 = (h0 | h1 << 26); h1 = (h1 >> 6 | h2 << 20); h2 = (h2 >> 12 | h3 << 14); h3 = (h3 >> 18 | h4 << 8);
    uint64_t f = (uint64_t)h0 + p->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + p->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + p->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + p->pad[3] + (f >> 32); h3 = (uint32_t)f;
    put_le32(tag, h0); put_le32(tag + 4, h1); put_le32(tag + 8, h2); put_le32(tag + 12, h3);
}
static void chacha_poly(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                        uint8_t *data, size_t length, uint8_t tag[16], int seal)
{
    uint8_t block[64], lengths[16];
    chacha_block(key, 0, nonce, block);
    Poly p; poly_init(&p, block);
    poly_update(&p, aad, aad_length);
    if (!seal) poly_update(&p, data, length);
    for (size_t at = 0; at < length; at += 64) {
        chacha_block(key, (uint32_t)(at / 64 + 1), nonce, block);
        for (size_t i = 0; i < 64 && at + i < length; i++) data[at + i] ^= block[i];
    }
    if (seal) poly_update(&p, data, length);
    put_le32(lengths, (uint32_t)aad_length); put_le32(lengths + 4, 0);
    put_le32(lengths + 8, (uint32_t)length); put_le32(lengths + 12, 0);
    poly_block(&p, lengths, 1U << 24);
    poly_final(&p, tag);
}
void chacha_poly_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                      uint8_t *data, size_t length, uint8_t tag[16])
{ chacha_poly(key, nonce, aad, aad_length, data, length, tag, 1); }
int chacha_poly_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                     uint8_t *data, size_t length, const uint8_t tag[16])
{
    uint8_t expected[16], diff = 0, copy[16];
    memcpy(copy, tag, 16);
    chacha_poly(key, nonce, aad, aad_length, data, length, expected, 0);
    for (int i = 0; i < 16; i++) diff |= expected[i] ^ copy[i];
    return diff ? -1 : 0;
}

/* ------------------------------------------------------------ X25519 */
typedef int64_t gf[16];
static void car(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        if (i < 15) o[i + 1] += c - 1; else o[0] += 38 * (c - 1);
        o[i] -= c << 16;
    }
}
static void sel(gf p, gf q, int b)
{
    int64_t c = ~((int64_t)b - 1);
    for (int i = 0; i < 16; i++) { int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
static void pack(uint8_t *o, const gf n)
{
    gf m, t;
    memcpy(t, n, sizeof(gf));
    car(t); car(t); car(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2 * i] = (uint8_t)t[i]; o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}
static void unpack(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}
static void fadd(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fsub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fmul(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car(o); car(o);
}
static void finv(gf o, const gf i)
{
    gf c;
    memcpy(c, i, sizeof(gf));
    for (int a = 253; a >= 0; a--) { fmul(c, c, c); if (a != 2 && a != 4) fmul(c, c, i); }
    memcpy(o, c, sizeof(gf));
}
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t z[32];
    gf x, a, b, c, d, e, f;
    static const gf k121665 = {0xdb41, 1};
    memcpy(z, scalar, 32);
    z[31] = (z[31] & 127) | 64; z[0] &= 248;
    unpack(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; a[i] = c[i] = d[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel(a, b, r); sel(c, d, r);
        fadd(e, a, c); fsub(a, a, c); fadd(c, b, d); fsub(b, b, d);
        fmul(d, e, e); fmul(f, a, a); fmul(a, c, a); fmul(c, b, e);
        fadd(e, a, c); fsub(a, a, c); fmul(b, a, a); fsub(c, d, f);
        fmul(a, c, k121665); fadd(a, a, d); fmul(c, c, a); fmul(a, d, f);
        fmul(d, b, x); fmul(b, e, e);
        sel(a, b, r); sel(c, d, r);
    }
    finv(c, c); fmul(a, a, c);
    pack(out, a);
}
void x25519_base(uint8_t out[32], const uint8_t scalar[32])
{
    static const uint8_t nine[32] = {9};
    x25519(out, scalar, nine);
}
