/* Barnix Net X.509: enough of RFC 5280 to check a TLS server: the chain up
 * to a trusted root, signatures (RSA PKCS#1/PSS, ECDSA P-256/P-384), dates,
 * the CA flag and the host name (subjectAltName, with wildcards).
 *
 * Roots file: records of (u16 subject length, subject DER, u16 key length,
 * SubjectPublicKeyInfo DER), big-endian; scripts/make_roots.py writes it
 * from the Mozilla CA bundle. */
#include "bnet.h"

static int oid_is(const Der *oid, const char *bytes, size_t length) { return oid->n == length && !memcmp(oid->p, bytes, length); }
#define OID(d, s) oid_is(d, s, sizeof(s) - 1)

/* Element with its header, for hashing and comparing raw names. */
static int der_raw(Der *d, int tag, Der *raw, Der *contents)
{
    const uint8_t *start = d->p;
    if (der_get(d, tag, contents)) return -1;
    raw->p = start; raw->n = (size_t)(d->p - start);
    return 0;
}
static int hash_oid(const Der *oid)
{
    if (OID(oid, "\x60\x86\x48\x01\x65\x03\x04\x02\x01")) return 256;
    if (OID(oid, "\x60\x86\x48\x01\x65\x03\x04\x02\x02")) return 384;
    if (OID(oid, "\x60\x86\x48\x01\x65\x03\x04\x02\x03")) return 512;
    if (OID(oid, "\x2b\x0e\x03\x02\x1a")) return 1;
    return 0;
}
static int signature_algorithm(Der alg, int *hash, int *kind)
{
    Der oid;
    if (der_get(&alg, 0x06, &oid)) return -1;
    static const char rsa[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x01";
    *hash = 0;
    if (oid.n == 9 && !memcmp(oid.p, rsa, 8)) {
        int last = oid.p[8];
        *kind = 1;
        *hash = last == 5 ? 1 : last == 11 ? 256 : last == 12 ? 384 : last == 13 ? 512 : 0;
        if (last == 10) {   /* RSASSA-PSS: the hash is in the parameters */
            Der params, h, hid;
            *kind = 3; *hash = 1;
            if (!der_get(&alg, 0x30, &params) && der_peek(&params) == 0xa0 &&
                !der_get(&params, 0xa0, &h) && !der_get(&h, 0x30, &hid) && !der_get(&hid, 0x06, &oid))
                *hash = hash_oid(&oid);
        }
    } else if (OID(&oid, "\x2a\x86\x48\xce\x3d\x04\x03\x02")) { *kind = 2; *hash = 256; }
    else if (OID(&oid, "\x2a\x86\x48\xce\x3d\x04\x03\x03")) { *kind = 2; *hash = 384; }
    else if (OID(&oid, "\x2a\x86\x48\xce\x3d\x04\x03\x04")) { *kind = 2; *hash = 512; }
    return *hash ? 0 : -1;
}
static int parse_key(Cert *c, Der spki_contents)
{
    Der alg, oid, bits;
    if (der_get(&spki_contents, 0x30, &alg) || der_get(&alg, 0x06, &oid) ||
        der_get(&spki_contents, 0x03, &bits) || !bits.n || bits.p[0]) return -1;
    bits.p++; bits.n--;
    c->key = bits;
    if (OID(&oid, "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01")) {
        Der seq;
        if (der_get(&bits, 0x30, &seq) || der_get(&seq, 0x02, &c->rsa_n) || der_get(&seq, 0x02, &c->rsa_e)) return -1;
        c->key_type = 1;
        return 0;
    }
    if (OID(&oid, "\x2a\x86\x48\xce\x3d\x02\x01")) {
        Der curve;
        if (der_get(&alg, 0x06, &curve)) return -1;
        if (OID(&curve, "\x2a\x86\x48\xce\x3d\x03\x01\x07")) c->key_type = 256;
        else if (OID(&curve, "\x2b\x81\x04\x00\x22")) c->key_type = 384;
        else return -1;
        return 0;
    }
    return -1;
}
static int parse_time(Der *d, uint8_t out[14])
{
    Der t;
    int tag = der_peek(d);
    if (tag == 0x17) {
        if (der_get(d, 0x17, &t) || t.n < 12) return -1;
        out[0] = t.p[0] < '5' ? '2' : '1'; out[1] = t.p[0] < '5' ? '0' : '9';
        memcpy(out + 2, t.p, 12);
        return 0;
    }
    if (der_get(d, 0x18, &t) || t.n < 14) return -1;
    memcpy(out, t.p, 14);
    return 0;
}
static void parse_extensions(Cert *c, Der list)
{
    Der seq, ext, oid, value;
    if (der_get(&list, 0x30, &seq)) return;
    while (seq.n && !der_get(&seq, 0x30, &ext)) {
        if (der_get(&ext, 0x06, &oid)) continue;
        if (der_peek(&ext) == 0x01) der_get(&ext, 0x01, 0);
        if (der_get(&ext, 0x04, &value)) continue;
        if (OID(&oid, "\x55\x1d\x11")) der_get(&value, 0x30, &c->san);
        if (OID(&oid, "\x55\x1d\x13")) {
            Der bc, flag;
            if (!der_get(&value, 0x30, &bc) && der_peek(&bc) == 0x01 && !der_get(&bc, 0x01, &flag) && flag.n == 1)
                c->ca = flag.p[0] != 0;
        }
    }
}
int cert_parse(const uint8_t *data, size_t length, Cert *c)
{
    memset(c, 0, sizeof(*c));
    Der d = {data, length}, cert, tbs, alg, spki;
    if (der_raw(&d, 0x30, &c->raw, &cert) || der_raw(&cert, 0x30, &c->tbs, &tbs) ||
        der_get(&cert, 0x30, &alg) || der_get(&cert, 0x03, &c->signature) || !c->signature.n) return -1;
    c->signature.p++; c->signature.n--;
    if (signature_algorithm(alg, &c->sig_hash, &c->sig_kind)) c->sig_kind = 0;
    if (der_peek(&tbs) == 0xa0) der_get(&tbs, 0xa0, 0);
    Der skip, validity, name;
    if (der_get(&tbs, 0x02, &skip) || der_get(&tbs, 0x30, &skip) ||
        der_raw(&tbs, 0x30, &c->issuer, &name) || der_get(&tbs, 0x30, &validity) ||
        parse_time(&validity, c->not_before) || parse_time(&validity, c->not_after) ||
        der_raw(&tbs, 0x30, &c->subject, &name) || der_raw(&tbs, 0x30, &c->spki, &spki)) return -1;
    if (parse_key(c, spki)) c->key_type = 0;
    while (tbs.n) {
        int tag = der_peek(&tbs);
        Der field;
        if (der_get(&tbs, -1, &field)) break;
        if (tag == 0xa3) parse_extensions(c, field);
    }
    return 0;
}
/* The signature of c over its TBS part, checked with key's public key. */
static int signed_with(const Cert *c, const Cert *key)
{
    uint8_t digest[64];
    if (!c->sig_kind || !key->key_type) return -1;
    unsigned int length = hash_digest(c->sig_hash, c->tbs.p, c->tbs.n, digest);
    if (c->sig_kind == 2)
        return key->key_type == 1 ? -1 : ecdsa_verify(key->key_type, key->key.p, key->key.n, digest, length,
                                                      c->signature.p, c->signature.n);
    if (key->key_type != 1) return -1;
    if (c->sig_kind == 3)
        return rsa_pss_verify(key->rsa_n.p, key->rsa_n.n, key->rsa_e.p, key->rsa_e.n, c->sig_hash, digest,
                              c->signature.p, c->signature.n);
    return rsa_pkcs1_verify(key->rsa_n.p, key->rsa_n.n, key->rsa_e.p, key->rsa_e.n, c->sig_hash, digest,
                            c->signature.p, c->signature.n);
}
int cert_signed_by(const Cert *c, const Cert *issuer) { return signed_with(c, issuer); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int name_match(const uint8_t *pattern, size_t length, const char *host)
{
    size_t host_length = strlen(host);
    if (length > 2 && pattern[0] == '*' && pattern[1] == '.') {
        const char *dot = host;
        while (*dot && *dot != '.') dot++;
        if (!*dot || dot == host) return 0;
        host = dot; host_length = strlen(host); pattern++; length--;
    }
    if (length != host_length) return 0;
    for (size_t i = 0; i < length; i++) if (lower(pattern[i]) != lower(host[i])) return 0;
    return 1;
}
int cert_hostname(const Cert *c, const char *host)
{
    Der san = c->san, name;
    while (san.n) {
        int tag = der_peek(&san);
        if (der_get(&san, -1, &name)) break;
        if (tag == 0x82 && name_match(name.p, name.n, host)) return 0;
    }
    return -1;
}
static int dated(const Cert *c, const char *now)
{
    return !now || (memcmp(now, c->not_before, 14) >= 0 && memcmp(now, c->not_after, 14) <= 0);
}
static int same(const Der *a, const Der *b) { return a->n == b->n && !memcmp(a->p, b->p, a->n); }
/* A root that signed c, from the roots file. */
static int signed_by_root(const Cert *c, const uint8_t *roots, size_t length)
{
    for (size_t at = 0; at + 4 <= length;) {
        size_t subject_length = (size_t)roots[at] << 8 | roots[at + 1];
        if (at + 2 + subject_length + 2 > length) break;
        Der subject = {roots + at + 2, subject_length};
        size_t key_length = (size_t)roots[at + 2 + subject_length] << 8 | roots[at + 3 + subject_length];
        const uint8_t *key = roots + at + 4 + subject_length;
        at += 4 + subject_length + key_length;
        if (at > length || !same(&subject, &c->issuer)) continue;
        Cert root;
        Der d = {key, key_length}, spki;
        memset(&root, 0, sizeof(root));
        if (der_get(&d, 0x30, &spki) || parse_key(&root, spki)) continue;
        if (!signed_with(c, &root)) return 0;
    }
    return -1;
}
const char *chain_verify(Cert *certs, int count, const char *host, const char *now,
                         const uint8_t *roots, size_t roots_length)
{
    if (count < 1) return "the server sent no certificate";
    if (cert_hostname(&certs[0], host)) return "the certificate is for another host name";
    const Cert *c = &certs[0];
    for (int depth = 0; depth < 8; depth++) {
        if (!dated(c, now)) return depth ? "an issuer certificate has expired (or the clock is wrong)"
                                         : "the certificate has expired (or the clock is wrong)";
        if (!signed_by_root(c, roots, roots_length)) return 0;
        const Cert *next = 0;
        for (int i = 0; i < count && !next; i++)
            if (&certs[i] != c && same(&certs[i].subject, &c->issuer) && certs[i].ca && !signed_with(c, &certs[i]))
                next = &certs[i];
        if (!next) return "the certificate is not issued by a trusted authority";
        c = next;
    }
    return "the certificate chain is too long";
}
