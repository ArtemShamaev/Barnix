/* Barnix Net public-key verification: Montgomery arithmetic on fixed-size
 * numbers, RSA PKCS#1 v1.5 and PSS, ECDSA on P-256 and P-384, DER reading.
 * Only verification: nothing here handles secrets. */
#include "bnet.h"

#define WORDS 130   /* 4160 bits: RSA up to 4096 */
typedef struct { uint32_t w[WORDS]; } Bn;
typedef struct { Bn m, rr, one; int n; uint32_t inv; } Mont;

static void bn_from(Bn *a, const uint8_t *p, size_t length, int n)
{
    memset(a, 0, sizeof(*a));
    for (size_t i = 0; i < length; i++) {
        size_t bit = (length - 1 - i) * 8;
        if (bit / 32 < (size_t)n) a->w[bit / 32] |= (uint32_t)p[i] << (bit % 32);
    }
}
static void bn_to(const Bn *a, uint8_t *p, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        size_t bit = (length - 1 - i) * 8;
        p[i] = bit / 32 < WORDS ? (uint8_t)(a->w[bit / 32] >> (bit % 32)) : 0;
    }
}
static int bn_cmp(const Bn *a, const Bn *b, int n)
{
    for (int i = n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] > b->w[i] ? 1 : -1;
    return 0;
}
static int bn_zero(const Bn *a, int n) { for (int i = 0; i < n; i++) if (a->w[i]) return 0; return 1; }
static uint32_t bn_sub(Bn *r, const Bn *a, const Bn *b, int n)
{
    uint32_t borrow = 0;
    for (int i = 0; i < n; i++) {
        uint64_t d = (uint64_t)a->w[i] - b->w[i] - borrow;
        r->w[i] = (uint32_t)d; borrow = (uint32_t)(d >> 32) & 1;
    }
    return borrow;
}
static uint32_t bn_add(Bn *r, const Bn *a, const Bn *b, int n)
{
    uint32_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t s = (uint64_t)a->w[i] + b->w[i] + carry;
        r->w[i] = (uint32_t)s; carry = (uint32_t)(s >> 32);
    }
    return carry;
}
static void mod_add(Bn *r, const Bn *a, const Bn *b, const Mont *m)
{
    uint32_t carry = bn_add(r, a, b, m->n);
    if (carry || bn_cmp(r, &m->m, m->n) >= 0) bn_sub(r, r, &m->m, m->n);
}
static void mod_sub(Bn *r, const Bn *a, const Bn *b, const Mont *m)
{
    if (bn_sub(r, a, b, m->n)) bn_add(r, r, &m->m, m->n);
}
static void mont_mul(Bn *r, const Bn *a, const Bn *b, const Mont *m)
{
    uint32_t t[WORDS + 2];
    int n = m->n;
    memset(t, 0, sizeof(uint32_t) * (n + 2));
    for (int i = 0; i < n; i++) {
        uint64_t c = 0;
        for (int j = 0; j < n; j++) {
            c += (uint64_t)a->w[j] * b->w[i] + t[j];
            t[j] = (uint32_t)c; c >>= 32;
        }
        c += t[n]; t[n] = (uint32_t)c; t[n + 1] = (uint32_t)(c >> 32);
        uint32_t q = t[0] * m->inv;
        c = (uint64_t)q * m->m.w[0] + t[0];
        c >>= 32;
        for (int j = 1; j < n; j++) {
            c += (uint64_t)q * m->m.w[j] + t[j];
            t[j - 1] = (uint32_t)c; c >>= 32;
        }
        c += t[n]; t[n - 1] = (uint32_t)c;
        t[n] = t[n + 1] + (uint32_t)(c >> 32);
    }
    Bn out;
    memset(&out, 0, sizeof(out));
    memcpy(out.w, t, sizeof(uint32_t) * n);
    if (t[n] || bn_cmp(&out, &m->m, n) >= 0) bn_sub(&out, &out, &m->m, n);
    *r = out;
}
/* Modulus big-endian; returns -1 for an even or oversized modulus. */
static int mont_init(Mont *m, const uint8_t *modulus, size_t length)
{
    while (length && !*modulus) { modulus++; length--; }
    if (!length || length > WORDS * 4 - 4 || !(modulus[length - 1] & 1)) return -1;
    m->n = (int)((length + 3) / 4);
    bn_from(&m->m, modulus, length, m->n);
    uint32_t x = 1;
    for (int i = 0; i < 5; i++) x *= 2 - m->m.w[0] * x;
    m->inv = 0 - x;
    Bn r;
    memset(&r, 0, sizeof(r)); r.w[0] = 1;
    for (int i = 0; i < 64 * m->n; i++) {   /* 2^(64n) mod m by doubling */
        uint32_t carry = bn_add(&r, &r, &r, m->n);
        if (carry || bn_cmp(&r, &m->m, m->n) >= 0) bn_sub(&r, &r, &m->m, m->n);
        if (i == 32 * m->n - 1) m->one = r;   /* R mod m */
    }
    m->rr = r;
    return 0;
}
static void to_mont(Bn *r, const Bn *a, const Mont *m) { mont_mul(r, a, &m->rr, m); }
static void from_mont(Bn *r, const Bn *a, const Mont *m)
{
    Bn one; memset(&one, 0, sizeof(one)); one.w[0] = 1;
    mont_mul(r, a, &one, m);
}
/* r = a^e in the Montgomery domain (a and r Montgomery form). */
static void mont_pow(Bn *r, const Bn *a, const uint8_t *e, size_t e_length, const Mont *m)
{
    Bn x = m->one;
    for (size_t i = 0; i < e_length; i++)
        for (int bit = 7; bit >= 0; bit--) {
            mont_mul(&x, &x, &x, m);
            if (e[i] >> bit & 1) mont_mul(&x, &x, a, m);
        }
    *r = x;
}

/* ------------------------------------------------------------ RSA */
static int rsa_public(const uint8_t *n, size_t n_length, const uint8_t *e, size_t e_length,
                      const uint8_t *signature, size_t signature_length, uint8_t *out, size_t *k, int *bits)
{
    Mont ctx;
    while (n_length && !*n) { n++; n_length--; }
    if (n_length < 128 || n_length > 512 || signature_length != n_length || mont_init(&ctx, n, n_length)) return -1;
    Bn s, x;
    bn_from(&s, signature, signature_length, ctx.n);
    if (bn_cmp(&s, &ctx.m, ctx.n) >= 0) return -1;
    to_mont(&x, &s, &ctx);
    mont_pow(&x, &x, e, e_length, &ctx);
    from_mont(&x, &x, &ctx);
    bn_to(&x, out, n_length);
    *k = n_length;
    int top = 7; while (top >= 0 && !(n[0] >> top & 1)) top--;
    *bits = (int)(n_length - 1) * 8 + top + 1;
    return 0;
}
static const uint8_t *digest_info(int hash, size_t *length)
{
    static const uint8_t sha1[] = {0x30,0x21,0x30,0x09,0x06,0x05,0x2b,0x0e,0x03,0x02,0x1a,0x05,0x00,0x04,0x14};
    static const uint8_t sha256[] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
    static const uint8_t sha384[] = {0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30};
    static const uint8_t sha512[] = {0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40};
    *length = hash == 1 ? sizeof(sha1) : sizeof(sha256);
    return hash == 1 ? sha1 : hash == 256 ? sha256 : hash == 384 ? sha384 : sha512;
}
static size_t hash_size(int hash) { return hash == 1 ? 20 : hash == 256 ? 32 : hash == 384 ? 48 : 64; }
int rsa_pkcs1_verify(const uint8_t *n, size_t n_length, const uint8_t *e, size_t e_length,
                     int hash, const uint8_t *digest, const uint8_t *signature, size_t signature_length)
{
    uint8_t em[512];
    size_t k, prefix_length, h = hash_size(hash);
    int bits;
    if (rsa_public(n, n_length, e, e_length, signature, signature_length, em, &k, &bits)) return -1;
    const uint8_t *prefix = digest_info(hash, &prefix_length);
    size_t t = prefix_length + h;
    if (k < t + 11 || em[0] != 0 || em[1] != 1) return -1;
    for (size_t i = 2; i < k - t - 1; i++) if (em[i] != 0xff) return -1;
    if (em[k - t - 1] != 0) return -1;
    return memcmp(em + k - t, prefix, prefix_length) || memcmp(em + k - h, digest, h) ? -1 : 0;
}
int rsa_pss_verify(const uint8_t *n, size_t n_length, const uint8_t *e, size_t e_length,
                   int hash, const uint8_t *digest, const uint8_t *signature, size_t signature_length)
{
    uint8_t buffer[512], db[512], mprime[8 + 64 + 512], check[64];
    size_t k, h = hash_size(hash);
    int bits;
    if (rsa_public(n, n_length, e, e_length, signature, signature_length, buffer, &k, &bits)) return -1;
    int em_bits = bits - 1;
    size_t em_length = (size_t)(em_bits + 7) / 8;
    const uint8_t *em = buffer + (k - em_length);
    if (k > em_length && buffer[0]) return -1;
    if (em_length < h + 2 || em[em_length - 1] != 0xbc) return -1;
    size_t db_length = em_length - h - 1;
    const uint8_t *hh = em + db_length;
    int clear = 8 * (int)em_length - em_bits;
    if (em[0] & (0xff << (8 - clear) & 0xff)) return -1;
    /* MGF1 */
    for (uint32_t counter = 0, done = 0; done < db_length; counter++) {
        uint8_t seed[64 + 4], mask[64];
        memcpy(seed, hh, h);
        seed[h] = counter >> 24; seed[h + 1] = counter >> 16; seed[h + 2] = counter >> 8; seed[h + 3] = counter;
        hash_digest(hash, seed, h + 4, mask);
        for (size_t i = 0; i < h && done < db_length; i++, done++) db[done] = em[done] ^ mask[i];
    }
    db[0] &= 0xff >> clear;
    size_t i = 0;
    while (i < db_length && !db[i]) i++;
    if (i == db_length || db[i] != 1) return -1;
    size_t salt = db_length - i - 1;
    memset(mprime, 0, 8);
    memcpy(mprime + 8, digest, h);
    memcpy(mprime + 8 + h, db + i + 1, salt);
    hash_digest(hash, mprime, 8 + h + salt, check);
    return memcmp(check, hh, h) ? -1 : 0;
}

/* ------------------------------------------------------------ ECDSA */
typedef struct { Bn x, y, z; } Point;   /* Jacobian, Montgomery form; z = 0 is infinity */
typedef struct { Mont p, n; Bn b3; int bytes; Point g; } Curve;
static void hex(uint8_t *out, const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = text[2 * i + k];
            v = v * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
        }
        out[i] = (uint8_t)v;
    }
}
static int curve_init(Curve *c, int bits)
{
    static const char *const p256[4] = {
        "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF",
        "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
        "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
        "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5"};
    static const char *const p384[4] = {
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF",
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973",
        "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7",
        "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F"};
    const char *const *v = bits == 256 ? p256 : bits == 384 ? p384 : 0;
    if (!v) return -1;
    uint8_t buffer[48], gx[48], gy[48];
    c->bytes = bits / 8;
    hex(buffer, v[0], c->bytes); mont_init(&c->p, buffer, c->bytes);
    hex(buffer, v[1], c->bytes); mont_init(&c->n, buffer, c->bytes);
    hex(gx, v[2], c->bytes); hex(gy, v[3], c->bytes);
    Bn t;
    bn_from(&t, gx, c->bytes, c->p.n); to_mont(&c->g.x, &t, &c->p);
    bn_from(&t, gy, c->bytes, c->p.n); to_mont(&c->g.y, &t, &c->p);
    c->g.z = c->p.one;
    return 0;
}
static void point_double(Point *r, const Point *a, const Mont *p)
{
    if (bn_zero(&a->z, p->n)) { *r = *a; return; }
    Bn delta, gamma, beta, alpha, t1, t2 = {{0}}, x3, y3, z3 = {{0}};
    mont_mul(&delta, &a->z, &a->z, p);
    mont_mul(&gamma, &a->y, &a->y, p);
    mont_mul(&beta, &a->x, &gamma, p);
    mod_sub(&t1, &a->x, &delta, p); mod_add(&t2, &a->x, &delta, p);
    mont_mul(&alpha, &t1, &t2, p);
    mod_add(&t1, &alpha, &alpha, p); mod_add(&alpha, &t1, &alpha, p);           /* 3(x-d)(x+d) */
    mont_mul(&x3, &alpha, &alpha, p);
    mod_add(&t1, &beta, &beta, p); mod_add(&t1, &t1, &t1, p);                  /* 4 beta */
    mod_add(&t2, &t1, &t1, p); mod_sub(&x3, &x3, &t2, p);                       /* - 8 beta */
    mod_add(&z3, &a->y, &a->z, p); mont_mul(&z3, &z3, &z3, p);
    mod_sub(&z3, &z3, &gamma, p); mod_sub(&z3, &z3, &delta, p);
    mod_sub(&t1, &t1, &x3, p); mont_mul(&y3, &alpha, &t1, p);
    mont_mul(&t2, &gamma, &gamma, p);
    mod_add(&t2, &t2, &t2, p); mod_add(&t2, &t2, &t2, p); mod_add(&t2, &t2, &t2, p);
    mod_sub(&y3, &y3, &t2, p);
    r->x = x3; r->y = y3; r->z = z3;
}
static void point_add(Point *r, const Point *a, const Point *b, const Mont *p)
{
    if (bn_zero(&a->z, p->n)) { *r = *b; return; }
    if (bn_zero(&b->z, p->n)) { *r = *a; return; }
    Bn z1z1, z2z2, u1, u2, s1, s2, h, rr, hh, hhh, t;
    mont_mul(&z1z1, &a->z, &a->z, p); mont_mul(&z2z2, &b->z, &b->z, p);
    mont_mul(&u1, &a->x, &z2z2, p); mont_mul(&u2, &b->x, &z1z1, p);
    mont_mul(&s1, &a->y, &b->z, p); mont_mul(&s1, &s1, &z2z2, p);
    mont_mul(&s2, &b->y, &a->z, p); mont_mul(&s2, &s2, &z1z1, p);
    if (!bn_cmp(&u1, &u2, p->n)) {
        if (!bn_cmp(&s1, &s2, p->n)) { point_double(r, a, p); return; }
        memset(r, 0, sizeof(*r)); return;
    }
    mod_sub(&h, &u2, &u1, p); mod_sub(&rr, &s2, &s1, p);
    mont_mul(&hh, &h, &h, p); mont_mul(&hhh, &hh, &h, p);
    Point out;
    mont_mul(&t, &u1, &hh, p);                                      /* U1 H^2 */
    mont_mul(&out.x, &rr, &rr, p); mod_sub(&out.x, &out.x, &hhh, p);
    mod_sub(&out.x, &out.x, &t, p); mod_sub(&out.x, &out.x, &t, p);
    mod_sub(&t, &t, &out.x, p); mont_mul(&out.y, &rr, &t, p);
    mont_mul(&t, &s1, &hhh, p); mod_sub(&out.y, &out.y, &t, p);
    mont_mul(&out.z, &h, &a->z, p); mont_mul(&out.z, &out.z, &b->z, p);
    *r = out;
}
int der_peek(const Der *d) { return d->n ? d->p[0] : -1; }
int der_get(Der *d, int tag, Der *out)
{
    if (d->n < 2 || (tag >= 0 && d->p[0] != tag)) return -1;
    size_t length = d->p[1], header = 2;
    if (length & 0x80) {
        int count = length & 0x7f;
        if (count < 1 || count > 3 || d->n < 2 + (size_t)count) return -1;
        length = 0;
        for (int i = 0; i < count; i++) length = length << 8 | d->p[2 + i];
        header += count;
    }
    if (length > d->n - header) return -1;
    if (out) { out->p = d->p + header; out->n = length; }
    d->p += header + length; d->n -= header + length;
    return 0;
}
static int der_integer(Der *d, uint8_t *out, size_t size)
{
    Der v;
    if (der_get(d, 0x02, &v) || !v.n || (v.p[0] & 0x80)) return -1;
    while (v.n > 1 && !v.p[0]) { v.p++; v.n--; }
    if (v.n > size) return -1;
    memset(out, 0, size);
    memcpy(out + size - v.n, v.p, v.n);
    return 0;
}
int ecdsa_verify(int bits, const uint8_t *point, size_t point_length, const uint8_t *digest, size_t digest_length,
                 const uint8_t *signature, size_t signature_length)
{
    Curve c;
    if (curve_init(&c, bits) || point_length != 1 + 2 * (size_t)c.bytes || point[0] != 4) return -1;
    uint8_t rb[48], sb[48], zb[48];
    Der d = {signature, signature_length}, seq;
    if (der_get(&d, 0x30, &seq) || der_integer(&seq, rb, c.bytes) || der_integer(&seq, sb, c.bytes)) return -1;
    int n = c.n.n;
    Bn r, s, z, w, u1, u2, t;
    bn_from(&r, rb, c.bytes, n); bn_from(&s, sb, c.bytes, n);
    if (bn_zero(&r, n) || bn_zero(&s, n) || bn_cmp(&r, &c.n.m, n) >= 0 || bn_cmp(&s, &c.n.m, n) >= 0) return -1;
    memset(zb, 0, sizeof(zb));
    if (digest_length >= (size_t)c.bytes) memcpy(zb, digest, c.bytes);
    else memcpy(zb + c.bytes - digest_length, digest, digest_length);
    bn_from(&z, zb, c.bytes, n);
    if (bn_cmp(&z, &c.n.m, n) >= 0) bn_sub(&z, &z, &c.n.m, n);
    /* w = s^(n-2) mod n */
    uint8_t exponent[48];
    Bn two; memset(&two, 0, sizeof(two)); two.w[0] = 2;
    bn_sub(&t, &c.n.m, &two, n); bn_to(&t, exponent, c.bytes);
    to_mont(&w, &s, &c.n); mont_pow(&w, &w, exponent, c.bytes, &c.n);   /* Montgomery form */
    to_mont(&t, &z, &c.n); mont_mul(&u1, &t, &w, &c.n); from_mont(&u1, &u1, &c.n);
    to_mont(&t, &r, &c.n); mont_mul(&u2, &t, &w, &c.n); from_mont(&u2, &u2, &c.n);
    Point q, gq, acc;
    bn_from(&t, point + 1, c.bytes, c.p.n); to_mont(&q.x, &t, &c.p);
    bn_from(&t, point + 1 + c.bytes, c.bytes, c.p.n); to_mont(&q.y, &t, &c.p);
    q.z = c.p.one;
    point_add(&gq, &c.g, &q, &c.p);
    memset(&acc, 0, sizeof(acc));
    for (int bit = c.bytes * 8 - 1; bit >= 0; bit--) {
        point_double(&acc, &acc, &c.p);
        int a = u1.w[bit / 32] >> (bit % 32) & 1, b = u2.w[bit / 32] >> (bit % 32) & 1;
        if (a && b) point_add(&acc, &acc, &gq, &c.p);
        else if (a) point_add(&acc, &acc, &c.g, &c.p);
        else if (b) point_add(&acc, &acc, &q, &c.p);
    }
    if (bn_zero(&acc.z, c.p.n)) return -1;
    /* x = X / Z^2 */
    Bn zinv, x;
    bn_sub(&t, &c.p.m, &two, c.p.n); bn_to(&t, exponent, c.bytes);
    mont_mul(&zinv, &acc.z, &acc.z, &c.p);
    mont_pow(&zinv, &zinv, exponent, c.bytes, &c.p);
    mont_mul(&x, &acc.x, &zinv, &c.p); from_mont(&x, &x, &c.p);
    if (bn_cmp(&x, &c.n.m, n) >= 0) bn_sub(&x, &x, &c.n.m, n);
    return bn_cmp(&x, &r, n) ? -1 : 0;
}

/* ------------------------------------------------------------ ECDH P-256 */
/* For TLS key exchange with servers without X25519. The private key is the
 * client's ephemeral one: the plain double-and-add below is not constant
 * time, acceptable for one-off session keys on Barnix. */
static void point_mul(Point *r, const Point *a, const uint8_t *scalar, size_t length, const Mont *p)
{
    Point acc;
    memset(&acc, 0, sizeof(acc));
    for (size_t i = 0; i < length; i++)
        for (int bit = 7; bit >= 0; bit--) {
            point_double(&acc, &acc, p);
            if (scalar[i] >> bit & 1) point_add(&acc, &acc, a, p);
        }
    *r = acc;
}
static int affine(uint8_t *out, const Point *a, const Curve *c)
{
    if (bn_zero(&a->z, c->p.n)) return -1;
    uint8_t exponent[48];
    Bn t, two, zinv, z2, z3, x, y;
    memset(&two, 0, sizeof(two)); two.w[0] = 2;
    bn_sub(&t, &c->p.m, &two, c->p.n); bn_to(&t, exponent, c->bytes);
    mont_pow(&zinv, &a->z, exponent, c->bytes, &c->p);
    mont_mul(&z2, &zinv, &zinv, &c->p); mont_mul(&z3, &z2, &zinv, &c->p);
    mont_mul(&x, &a->x, &z2, &c->p); from_mont(&x, &x, &c->p);
    mont_mul(&y, &a->y, &z3, &c->p); from_mont(&y, &y, &c->p);
    out[0] = 4; bn_to(&x, out + 1, c->bytes); bn_to(&y, out + 1 + c->bytes, c->bytes);
    return 0;
}
/* private: 32 random bytes, made a valid scalar in place. */
int p256_public(uint8_t private_key[32], uint8_t public_key[65])
{
    Curve c;
    curve_init(&c, 256);
    Bn d;
    bn_from(&d, private_key, 32, c.n.n);
    if (bn_cmp(&d, &c.n.m, c.n.n) >= 0) bn_sub(&d, &d, &c.n.m, c.n.n);
    if (bn_zero(&d, c.n.n)) d.w[0] = 1;
    bn_to(&d, private_key, 32);
    Point q;
    point_mul(&q, &c.g, private_key, 32, &c.p);
    return affine(public_key, &q, &c);
}
int p256_shared(const uint8_t private_key[32], const uint8_t *peer, size_t peer_length, uint8_t shared[32])
{
    static const char b_hex[] = "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";
    Curve c;
    curve_init(&c, 256);
    if (peer_length != 65 || peer[0] != 4) return -1;
    Point q;
    Bn t, x3, y2, b;
    uint8_t bb[32];
    bn_from(&t, peer + 1, 32, c.p.n); if (bn_cmp(&t, &c.p.m, c.p.n) >= 0) return -1; to_mont(&q.x, &t, &c.p);
    bn_from(&t, peer + 33, 32, c.p.n); if (bn_cmp(&t, &c.p.m, c.p.n) >= 0) return -1; to_mont(&q.y, &t, &c.p);
    q.z = c.p.one;
    /* on the curve: y^2 = x^3 - 3x + b */
    hex(bb, b_hex, 32); bn_from(&t, bb, 32, c.p.n); to_mont(&b, &t, &c.p);
    mont_mul(&y2, &q.y, &q.y, &c.p);
    mont_mul(&x3, &q.x, &q.x, &c.p); mont_mul(&x3, &x3, &q.x, &c.p);
    mod_sub(&x3, &x3, &q.x, &c.p); mod_sub(&x3, &x3, &q.x, &c.p); mod_sub(&x3, &x3, &q.x, &c.p);
    mod_add(&x3, &x3, &b, &c.p);
    if (bn_cmp(&x3, &y2, c.p.n)) return -1;
    Point r;
    point_mul(&r, &q, private_key, 32, &c.p);
    uint8_t point[65];
    if (affine(point, &r, &c)) return -1;
    memcpy(shared, point + 1, 32);
    return 0;
}
