/* Barnix Net TLS client: TLS 1.3 (RFC 8446) and TLS 1.2 (RFC 5246) with
 * ECDHE over X25519 or P-256 and AES-128-GCM or ChaCha20-Poly1305; the
 * server is authenticated by its certificate chain (x509.c) and its
 * signature over the key exchange. No resumption, no renegotiation, no
 * client certificates (an empty list is sent when one is asked for). */
#include "bnet.h"
#include "tls.h"

#define RECORD_MAX (16384 + 256)
enum { CHANGE_CIPHER = 20, ALERT = 21, HANDSHAKE = 22, APPLICATION = 23 };

static void put16(uint8_t *p, unsigned int v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put24(uint8_t *p, unsigned int v) { p[0] = v >> 16; p[1] = v >> 8; p[2] = (uint8_t)v; }
static unsigned int get16(const uint8_t *p) { return (unsigned int)p[0] << 8 | p[1]; }
static unsigned int get24(const uint8_t *p) { return (unsigned int)p[0] << 16 | (unsigned int)p[1] << 8 | p[2]; }

static int fail(Tls *t, const char *text) { if (!t->error) t->error = text; t->state = TLS_FAILED; return -1; }
static int gcm(const Tls *t) { return t->cipher == 0x1301 || t->cipher == 0xc02b || t->cipher == 0xc02f; }

static int transport_send(Tls *t, const uint8_t *data, unsigned int length)
{
    while (length) {
        int sent = t->send(t->context, data, length);
        if (sent <= 0) return fail(t, "the connection was lost while sending");
        data += sent; length -= (unsigned int)sent;
    }
    return 0;
}
/* Fills t->record with one whole record; returns its type, 0 at a clean end. */
static int read_record(Tls *t, unsigned int timeout_ms)
{
    unsigned int need = 5;
    t->record_length = 0;
    while (t->record_length < need) {
        int got = t->recv(t->context, t->record + t->record_length, need - t->record_length, timeout_ms);
        if (got == 0 && !t->record_length) return 0;
        if (got <= 0) return fail(t, got == -2 ? "the server stopped answering" : "the connection was lost");
        t->record_length += (unsigned int)got;
        if (t->record_length == 5 && need == 5) {
            need = 5 + get16(t->record + 3);
            if (need > 5 + RECORD_MAX) return fail(t, "the server sent an oversized record");
        }
    }
    return t->record[0];
}
static void nonce(const uint8_t iv[12], uint64_t sequence, uint8_t out[12])
{
    memcpy(out, iv, 12);
    for (int i = 0; i < 8; i++) out[11 - i] ^= (uint8_t)(sequence >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i)); }
static void set_keys13(Tls *t, int server, const uint8_t secret[32])
{
    TlsKeys *k = server ? &t->server : &t->client;
    memcpy(k->secret, secret, 32);
    hkdf_expand_label(secret, "key", 0, 0, k->key, t->key_length);
    hkdf_expand_label(secret, "iv", 0, 0, k->iv, 12);
    k->sequence = 0;
}
static int aead_open(const Tls *t, const TlsKeys *k, const uint8_t n[12], const uint8_t *aad, unsigned int aad_length,
                     uint8_t *data, unsigned int length)
{
    return gcm(t) ? aes_gcm_open(k->key, n, aad, aad_length, data, length, data + length)
                  : chacha_poly_open(k->key, n, aad, aad_length, data, length, data + length);
}
static void aead_seal(const Tls *t, const TlsKeys *k, const uint8_t n[12], const uint8_t *aad, unsigned int aad_length,
                      uint8_t *data, unsigned int length)
{
    if (gcm(t)) aes_gcm_seal(k->key, n, aad, aad_length, data, length, data + length);
    else chacha_poly_seal(k->key, n, aad, aad_length, data, length, data + length);
}
/* Decrypts t->record in place: payload at t->plain; returns the content type. */
static int open_record(Tls *t)
{
    unsigned int length = t->record_length - 5;
    uint8_t *data = t->record + 5, n[12];
    if (t->version == 13) {
        if (length < 17) return fail(t, "the server sent a damaged record");
        nonce(t->server.iv, t->server.sequence++, n);
        if (aead_open(t, &t->server, n, t->record, 5, data, length - 16)) return fail(t, "a record failed authentication");
        length -= 16;
        while (length && !data[length - 1]) length--;
        if (!length) return fail(t, "the server sent an empty record");
        t->plain = data; t->plain_length = length - 1;
        return data[length - 1];
    }
    uint8_t aad[13];
    unsigned int explicit_length = gcm(t) ? 8 : 0;
    if (length < explicit_length + 16) return fail(t, "the server sent a damaged record");
    unsigned int plain = length - explicit_length - 16;
    if (gcm(t)) { memcpy(n, t->server.iv, 4); memcpy(n + 4, data, 8); }
    else nonce(t->server.iv, t->server.sequence, n);
    put64(aad, t->server.sequence++);
    aad[8] = t->record[0]; aad[9] = 3; aad[10] = 3; put16(aad + 11, plain);
    data += explicit_length;
    if (aead_open(t, &t->server, n, aad, 13, data, plain)) return fail(t, "a record failed authentication");
    t->plain = data; t->plain_length = plain;
    return t->record[0];
}
static int seal_record(Tls *t, int type, const uint8_t *data, unsigned int length)
{
    uint8_t *out = t->out, n[12];
    if (t->version == 13) {
        out[0] = APPLICATION; out[1] = 3; out[2] = 3; put16(out + 3, length + 17);
        memcpy(out + 5, data, length);
        out[5 + length] = (uint8_t)type;
        nonce(t->client.iv, t->client.sequence++, n);
        aead_seal(t, &t->client, n, out, 5, out + 5, length + 1);
        return transport_send(t, out, 5 + length + 17);
    }
    unsigned int explicit_length = gcm(t) ? 8 : 0;
    uint8_t aad[13];
    out[0] = (uint8_t)type; out[1] = 3; out[2] = 3; put16(out + 3, explicit_length + length + 16);
    if (gcm(t)) { memcpy(n, t->client.iv, 4); put64(n + 4, t->client.sequence); memcpy(out + 5, n + 4, 8); }
    else nonce(t->client.iv, t->client.sequence, n);
    put64(aad, t->client.sequence++);
    aad[8] = (uint8_t)type; aad[9] = 3; aad[10] = 3; put16(aad + 11, length);
    memcpy(out + 5 + explicit_length, data, length);
    aead_seal(t, &t->client, n, aad, 13, out + 5 + explicit_length, length);
    return transport_send(t, out, 5 + explicit_length + length + 16);
}
static void transcript_hash(const Tls *t, uint8_t out[32])
{
    Sha256 copy = t->transcript;
    sha256_final(&copy, out);
}
static void derive(const uint8_t secret[32], const char *label, const uint8_t hash[32], uint8_t out[32])
{
    hkdf_expand_label(secret, label, hash, 32, out, 32);
}
static void finished13(const uint8_t base[32], const uint8_t hash[32], uint8_t out[32])
{
    uint8_t key[32];
    hkdf_expand_label(base, "finished", 0, 0, key, 32);
    hmac_sha256(key, 32, hash, 32, out);
}
/* TLS 1.2 PRF with SHA-256. */
static void prf(const uint8_t *secret, unsigned int secret_length, const char *label,
                const uint8_t *seed, unsigned int seed_length, uint8_t *out, unsigned int length)
{
    uint8_t a[32], buffer[32 + 64 + 64], block[32];
    unsigned int label_length = (unsigned int)strlen(label), full = label_length + seed_length;
    memcpy(buffer + 32, label, label_length);
    memcpy(buffer + 32 + label_length, seed, seed_length);
    hmac_sha256(secret, secret_length, buffer + 32, full, a);
    for (unsigned int done = 0; done < length;) {
        memcpy(buffer, a, 32);
        hmac_sha256(secret, secret_length, buffer, 32 + full, block);
        unsigned int part = length - done < 32 ? length - done : 32;
        memcpy(out + done, block, part); done += part;
        hmac_sha256(secret, secret_length, a, 32, a);
    }
}

static int client_hello(Tls *t, const char *host, const uint8_t random[96])
{
    uint8_t *m = t->out + 5, *p = m + 4;
    unsigned int host_length = (unsigned int)strlen(host);
    if (host_length > 200) return fail(t, "the host name is too long");
    memcpy(t->x25519_private, random + 64, 32);
    x25519_base(t->x25519_public, t->x25519_private);
    p256_public(t->p256_private, t->p256_public_key);
    *p++ = 3; *p++ = 3;
    memcpy(t->client_random, random, 32);
    memcpy(p, random, 32); p += 32;
    *p++ = 32; memcpy(p, random + 32, 32); p += 32;          /* compatibility session id */
    static const uint8_t suites[] = {0x00, 0x0c, 0x13, 0x01, 0x13, 0x03, 0xc0, 0x2b, 0xc0, 0x2f, 0xcc, 0xa9, 0xcc, 0xa8, 1, 0};
    memcpy(p, suites, sizeof(suites)); p += sizeof(suites);
    uint8_t *extensions = p; p += 2;
    put16(p, 0); put16(p + 2, host_length + 5); put16(p + 4, host_length + 3); p[6] = 0;
    put16(p + 7, host_length); memcpy(p + 9, host, host_length); p += 9 + host_length;
    static const uint8_t fixed[] = {
        0x00, 0x0a, 0x00, 0x06, 0x00, 0x04, 0x00, 0x1d, 0x00, 0x17,              /* groups: x25519, P-256 */
        0x00, 0x0b, 0x00, 0x02, 0x01, 0x00,                                      /* uncompressed points */
        0xff, 0x01, 0x00, 0x01, 0x00,                                            /* no renegotiation */
        0x00, 0x0d, 0x00, 0x12, 0x00, 0x10, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04,  /* signatures */
        0x08, 0x05, 0x08, 0x06, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01,
        0x00, 0x2b, 0x00, 0x05, 0x04, 0x03, 0x04, 0x03, 0x03,                    /* TLS 1.3, 1.2 */
        0x00, 0x10, 0x00, 0x0b, 0x00, 0x09, 0x08, 'h', 't', 't', 'p', '/', '1', '.', '1',
        0x00, 0x33, 0x00, 0x6b, 0x00, 0x69, 0x00, 0x1d, 0x00, 0x20};           /* key shares */
    memcpy(p, fixed, sizeof(fixed)); p += sizeof(fixed);
    memcpy(p, t->x25519_public, 32); p += 32;
    put16(p, 0x17); put16(p + 2, 65); memcpy(p + 4, t->p256_public_key, 65); p += 69;
    put16(extensions, (unsigned int)(p - extensions - 2));
    unsigned int length = (unsigned int)(p - m);
    m[0] = 1; put24(m + 1, length - 4);
    sha256_update(&t->transcript, m, length);
    t->out[0] = HANDSHAKE; t->out[1] = 3; t->out[2] = 1; put16(t->out + 3, length);
    return transport_send(t, t->out, 5 + length);
}
static int shared_secret(Tls *t, unsigned int group, const uint8_t *key, unsigned int length, uint8_t shared[32])
{
    if (group == 0x1d && length == 32) { x25519(shared, t->x25519_private, key); return 0; }
    if (group == 0x17 && length == 65 && !p256_shared(t->p256_private, key, length, shared)) return 0;
    return fail(t, "the server sent an unusable key");
}
static int server_hello(Tls *t, const uint8_t *m, unsigned int length)
{
    static const uint8_t retry[32] = {0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
                                      0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c};
    if (length < 4 + 2 + 32 + 1 || m[0] != 2) return fail(t, "the server did not answer with TLS");
    const uint8_t *p = m + 4, *end = m + length;
    if (!memcmp(p + 2, retry, 32)) return fail(t, "the server wants a key exchange Barnix does not offer");
    if (get16(p) != 0x0303) return fail(t, "the server only speaks TLS 1.1 or older");
    memcpy(t->server_random, p + 2, 32);
    p += 34;
    p += 1 + *p;
    if (p + 3 > end) return fail(t, "damaged ServerHello");
    t->cipher = (int)get16(p); p += 3;
    unsigned int total = p + 2 <= end ? get16(p) : 0;
    p += 2;
    if (p + total > end) return fail(t, "damaged ServerHello");
    int share = 0;
    t->version = 12;
    for (const uint8_t *e = p; e + 4 <= p + total;) {
        unsigned int type = get16(e), size = get16(e + 2);
        const uint8_t *v = e + 4;
        if (v + size > p + total) break;
        if (type == 0x2b && size == 2 && get16(v) == 0x0304) t->version = 13;
        if (type == 0x33 && size >= 4 && get16(v + 2) == size - 4) {
            if (shared_secret(t, get16(v), v + 4, size - 4, t->shared)) return -1;
            share = 1;
        }
        e = v + size;
    }
    if (t->version == 13) {
        if (t->cipher != 0x1301 && t->cipher != 0x1303) return fail(t, "the server chose an unknown cipher");
        if (!share) return fail(t, "the server sent no key share");
        t->key_length = t->cipher == 0x1301 ? 16 : 32;
    } else {
        if (t->cipher != 0xc02b && t->cipher != 0xc02f && t->cipher != 0xcca9 && t->cipher != 0xcca8)
            return fail(t, "the server chose an unknown cipher");
        t->key_length = gcm(t) ? 16 : 32;
    }
    return 0;
}
static int certificate(Tls *t, const uint8_t *m, unsigned int length)
{
    const uint8_t *p = m + 4, *end = m + length;
    if (t->version == 13) { if (p >= end) return fail(t, "damaged Certificate"); p += 1 + *p; }
    if (p + 3 > end) return fail(t, "damaged Certificate");
    p += 3;
    t->cert_count = 0;
    while (p + 3 <= end && t->cert_count < TLS_CERTS) {
        unsigned int size = get24(p); p += 3;
        if (p + size > end) return fail(t, "damaged Certificate");
        if (cert_parse(p, size, &t->certs[t->cert_count])) return fail(t, "the server sent an unreadable certificate");
        t->cert_count++;
        p += size;
        if (t->version == 13) { if (p + 2 > end) return fail(t, "damaged Certificate"); p += 2 + get16(p); }
    }
    if (!t->cert_count) return fail(t, "the server sent no certificate");
    return 0;
}
/* Server signature over content with the leaf key. */
static int check_signature(Tls *t, unsigned int scheme, const uint8_t *content, unsigned int content_length,
                           const uint8_t *signature, unsigned int size)
{
    const Cert *leaf = &t->certs[0];
    uint8_t digest[64];
    int hash = (scheme >> 8) == 4 ? 256 : (scheme >> 8) == 5 ? 384 : (scheme >> 8) == 6 ? 512 : 0;
    if (scheme >= 0x0804 && scheme <= 0x0806) hash = scheme == 0x0804 ? 256 : scheme == 0x0805 ? 384 : 512;
    if (!hash) return -1;
    unsigned int n = hash_digest(hash, content, content_length, digest);
    if ((scheme & 0xff) == 3 && leaf->key_type >= 256 && (t->version == 12 || leaf->key_type == hash))
        return ecdsa_verify(leaf->key_type, leaf->key.p, leaf->key.n, digest, n, signature, size);
    if (leaf->key_type != 1) return -1;
    if (scheme >= 0x0804 && scheme <= 0x0806)
        return rsa_pss_verify(leaf->rsa_n.p, leaf->rsa_n.n, leaf->rsa_e.p, leaf->rsa_e.n, hash, digest, signature, size);
    if ((scheme & 0xff) == 1 && t->version == 12)
        return rsa_pkcs1_verify(leaf->rsa_n.p, leaf->rsa_n.n, leaf->rsa_e.p, leaf->rsa_e.n, hash, digest, signature, size);
    return -1;
}
static int certificate_verify(Tls *t, const uint8_t *m, unsigned int length)
{
    if (length < 8 || 8 + get16(m + 6) > length) return fail(t, "damaged CertificateVerify");
    uint8_t content[64 + 34 + 32];
    memset(content, 0x20, 64);
    memcpy(content + 64, "TLS 1.3, server CertificateVerify", 34);
    transcript_hash(t, content + 98);
    if (check_signature(t, get16(m + 4), content, sizeof(content), m + 8, get16(m + 6)))
        return fail(t, "the server could not prove it owns the certificate");
    return 0;
}
static int server_key_exchange(Tls *t, const uint8_t *m, unsigned int length)
{
    const uint8_t *p = m + 4, *end = m + length;
    if (p + 4 > end || p[0] != 3) return fail(t, "the server offered an unsupported key exchange");
    unsigned int group = get16(p + 1), size = p[3];
    if (p + 4 + size + 4 > end) return fail(t, "damaged ServerKeyExchange");
    const uint8_t *key = p + 4;
    unsigned int params = 4 + size;
    unsigned int scheme = get16(p + params), signature_length = get16(p + params + 2);
    if (p + params + 4 + signature_length > end) return fail(t, "damaged ServerKeyExchange");
    static uint8_t content[64 + 4 + 256];
    if (params > 4 + 133) return fail(t, "damaged ServerKeyExchange");
    memcpy(content, t->client_random, 32); memcpy(content + 32, t->server_random, 32);
    memcpy(content + 64, p, params);
    if (!t->cert_count || check_signature(t, scheme, content, 64 + params, p + params + 4, signature_length))
        return fail(t, "the server could not prove it owns the certificate");
    t->group = group;
    if (shared_secret(t, group, key, size, t->shared)) return -1;
    return 0;
}
/* Handshake messages arrive in records of any split: gather them in t->hs. */
static int next_message(Tls *t, const uint8_t **message, unsigned int *length)
{
    for (;;) {
        if (t->hs_length >= 4) {
            unsigned int size = 4 + get24(t->hs + 1);
            if (size > sizeof(t->hs)) return fail(t, "the server sent an oversized handshake message");
            if (t->hs_length >= size) { *message = t->hs; *length = size; return 0; }
        }
        int type = read_record(t, 20000);
        if (type <= 0) return fail(t, "the server closed the connection during the handshake");
        if (type == CHANGE_CIPHER) { if (t->version == 12) t->server_encrypted = 1; continue; }
        const uint8_t *data = t->record + 5;
        unsigned int size = t->record_length - 5;
        if (t->server_encrypted) {
            type = open_record(t);
            if (type < 0) return -1;
            data = t->plain; size = t->plain_length;
        }
        if (type == ALERT) return fail(t, size >= 2 && data[1] == 40 ? "the server refused the handshake"
                                          : size >= 2 && data[1] == 70 ? "the server wants another TLS version"
                                          : "the server sent an alert during the handshake");
        if (type != HANDSHAKE) return fail(t, "unexpected record during the handshake");
        if (t->hs_length + size > sizeof(t->hs)) return fail(t, "the handshake is too large");
        memcpy(t->hs + t->hs_length, data, size);
        t->hs_length += size;
    }
}
static void consume(Tls *t, unsigned int length)
{
    sha256_update(&t->transcript, t->hs, length);
    memmove(t->hs, t->hs + length, t->hs_length - length);
    t->hs_length -= length;
}
/* Certificates point into the message: keep a copy while t->hs shifts. */
static int copy_certificates(Tls *t, const uint8_t *m, unsigned int length)
{
    if (length > sizeof(t->chain)) return fail(t, "the certificate chain is too large");
    memcpy(t->chain, m, length);
    return certificate(t, t->chain, length);
}
static int check_chain(Tls *t, const char *host, const char *now, const uint8_t *roots, size_t roots_length)
{
    const char *problem = chain_verify(t->certs, t->cert_count, host, now, roots, roots_length);
    return problem ? fail(t, problem) : 0;
}
static const uint8_t ccs[6] = {CHANGE_CIPHER, 3, 3, 0, 1, 1};

static int handshake13(Tls *t, const char *host, const char *now, const uint8_t *roots, size_t roots_length)
{
    uint8_t hash[32], zero[32] = {0}, empty[32], secret[32], derived[32], client[32], server[32];
    if (t->hs_length) return fail(t, "the server mixed plaintext and encrypted handshake data");
    hash_digest(256, "", 0, empty);
    hkdf_extract(0, 0, zero, 32, secret);
    derive(secret, "derived", empty, derived);
    hkdf_extract(derived, 32, t->shared, 32, t->handshake_secret);
    transcript_hash(t, hash);
    derive(t->handshake_secret, "c hs traffic", hash, client);
    derive(t->handshake_secret, "s hs traffic", hash, server);
    set_keys13(t, 0, client); set_keys13(t, 1, server);
    t->server_encrypted = 1;
    int asked_certificate = 0, verified = 0;
    const uint8_t *m;
    unsigned int length;
    for (;;) {
        if (next_message(t, &m, &length)) return -1;
        int type = m[0];
        if (type == 13) asked_certificate = 1;
        else if (type == 11) { if (copy_certificates(t, m, length)) return -1; }
        else if (type == 15) {
            if (!t->cert_count || certificate_verify(t, m, length) || check_chain(t, host, now, roots, roots_length)) return -1;
            verified = 1;
        } else if (type == 20) {
            uint8_t expected[32];
            transcript_hash(t, hash);
            finished13(t->server.secret, hash, expected);
            if (!verified) return fail(t, "the server skipped authentication");
            if (length != 36 || memcmp(expected, m + 4, 32)) return fail(t, "the handshake was tampered with");
            consume(t, length);
            break;
        } else if (type != 8) return fail(t, "unexpected handshake message");
        consume(t, length);
    }
    transcript_hash(t, hash);   /* ClientHello..server Finished */
    uint8_t master[32], client_app[32], server_app[32];
    derive(t->handshake_secret, "derived", empty, derived);
    hkdf_extract(derived, 32, zero, 32, master);
    derive(master, "c ap traffic", hash, client_app);
    derive(master, "s ap traffic", hash, server_app);
    if (transport_send(t, ccs, 6)) return -1;
    if (asked_certificate) {
        uint8_t none[8] = {11, 0, 0, 4, 0, 0, 0, 0};
        sha256_update(&t->transcript, none, 8);
        if (seal_record(t, HANDSHAKE, none, 8)) return -1;
    }
    uint8_t done[36];
    transcript_hash(t, hash);
    done[0] = 20; put24(done + 1, 32);
    finished13(t->client.secret, hash, done + 4);
    if (seal_record(t, HANDSHAKE, done, 36)) return -1;
    set_keys13(t, 0, client_app); set_keys13(t, 1, server_app);
    return 0;
}
static int handshake12(Tls *t, const char *host, const char *now, const uint8_t *roots, size_t roots_length)
{
    int asked_certificate = 0, exchanged = 0;
    const uint8_t *m;
    unsigned int length;
    for (;;) {
        if (next_message(t, &m, &length)) return -1;
        int type = m[0];
        if (type == 11) { if (copy_certificates(t, m, length) || check_chain(t, host, now, roots, roots_length)) return -1; }
        else if (type == 12) { if (server_key_exchange(t, m, length)) return -1; exchanged = 1; }
        else if (type == 13) asked_certificate = 1;
        else if (type == 14) { consume(t, length); break; }
        else if (type != 22) return fail(t, "unexpected handshake message");
        consume(t, length);
    }
    if (!exchanged) return fail(t, "the server offered no forward-secret key exchange");
    /* Client flight: [Certificate], ClientKeyExchange, ChangeCipherSpec, Finished. */
    uint8_t *flight = t->out + 5, *p = flight;
    if (asked_certificate) { static const uint8_t none[7] = {11, 0, 0, 3, 0, 0, 0}; memcpy(p, none, 7); p += 7; }
    unsigned int key_length = t->group == 0x1d ? 32 : 65;
    p[0] = 16; put24(p + 1, key_length + 1); p[4] = (uint8_t)key_length;
    memcpy(p + 5, t->group == 0x1d ? t->x25519_public : t->p256_public_key, key_length);
    p += 5 + key_length;
    unsigned int flight_length = (unsigned int)(p - flight);
    sha256_update(&t->transcript, flight, flight_length);
    t->out[0] = HANDSHAKE; t->out[1] = 3; t->out[2] = 3; put16(t->out + 3, flight_length);
    if (transport_send(t, t->out, 5 + flight_length) || transport_send(t, ccs, 6)) return -1;
    uint8_t seed[64], master[48], block[2 * 32 + 2 * 12];
    memcpy(seed, t->client_random, 32); memcpy(seed + 32, t->server_random, 32);
    prf(t->shared, 32, "master secret", seed, 64, master, 48);
    memcpy(seed, t->server_random, 32); memcpy(seed + 32, t->client_random, 32);
    unsigned int iv_length = gcm(t) ? 4 : 12;
    prf(master, 48, "key expansion", seed, 64, block, 2 * t->key_length + 2 * iv_length);
    memcpy(t->client.key, block, t->key_length);
    memcpy(t->server.key, block + t->key_length, t->key_length);
    memcpy(t->client.iv, block + 2 * t->key_length, iv_length);
    memcpy(t->server.iv, block + 2 * t->key_length + iv_length, iv_length);
    t->client.sequence = t->server.sequence = 0;
    uint8_t hash[32], done[16];
    transcript_hash(t, hash);
    done[0] = 20; put24(done + 1, 12);
    prf(master, 48, "client finished", hash, 32, done + 4, 12);
    sha256_update(&t->transcript, done, 16);
    if (seal_record(t, HANDSHAKE, done, 16)) return -1;
    if (next_message(t, &m, &length)) return -1;
    if (!t->server_encrypted || m[0] != 20 || length != 16) return fail(t, "the handshake was tampered with");
    uint8_t expected[12];
    transcript_hash(t, hash);
    prf(master, 48, "server finished", hash, 32, expected, 12);
    if (memcmp(expected, m + 4, 12)) return fail(t, "the handshake was tampered with");
    consume(t, length);
    memset(master, 0, sizeof(master));
    return 0;
}

int tls_connect(Tls *t, const char *host, const uint8_t random[96], const uint8_t *roots, size_t roots_length,
                const char *now)
{
    void *context = t->context;
    int (*send)(void *, const void *, unsigned int) = t->send;
    int (*recv)(void *, void *, unsigned int, unsigned int) = t->recv;
    memset(t, 0, sizeof(*t));
    t->context = context; t->send = send; t->recv = recv;
    sha256_init(&t->transcript);
    hash_digest(256, random, 96, t->p256_private);   /* a second private key from the same randomness */
    if (client_hello(t, host, random)) return -1;
    const uint8_t *m;
    unsigned int length;
    if (next_message(t, &m, &length) || server_hello(t, m, length)) return -1;
    consume(t, length);
    int result = t->version == 13 ? handshake13(t, host, now, roots, roots_length)
                                  : handshake12(t, host, now, roots, roots_length);
    memset(t->shared, 0, 32); memset(t->x25519_private, 0, 32); memset(t->p256_private, 0, 32);
    if (result) return -1;
    t->state = TLS_OPEN;
    t->hs_length = 0;
    return 0;
}

int tls_write(Tls *t, const void *data, unsigned int length)
{
    if (t->state != TLS_OPEN) return -1;
    const uint8_t *p = data;
    unsigned int left = length;
    while (left) {
        unsigned int part = left > 16000 ? 16000 : left;
        if (seal_record(t, APPLICATION, p, part)) return -1;
        p += part; left -= part;
    }
    return (int)length;
}

int tls_read(Tls *t, void *data, unsigned int capacity, unsigned int timeout_ms)
{
    while (!t->pending) {
        if (t->state == TLS_CLOSED) return 0;
        if (t->state != TLS_OPEN) return -1;
        int type = read_record(t, timeout_ms);
        if (type == 0) { t->state = TLS_CLOSED; return 0; }   /* closed without close_notify */
        if (type < 0) return -1;
        if (type == CHANGE_CIPHER) continue;
        if (t->version == 13 && type != APPLICATION) return fail(t, "unexpected plaintext record");
        type = open_record(t);
        if (type < 0) return -1;
        if (type == APPLICATION) { if (t->plain_length) { t->pending = t->plain; t->pending_length = t->plain_length; } }
        else if (type == ALERT) {
            if (t->plain_length >= 2 && t->plain[1] == 0) { t->state = TLS_CLOSED; return 0; }
            return fail(t, "the server sent an alert");
        } else if (type == HANDSHAKE && t->version == 13) {
            /* NewSessionTicket is ignored; KeyUpdate renews the server key. */
            const uint8_t *m = t->plain;
            for (unsigned int at = 0; at + 4 <= t->plain_length; at += 4 + get24(m + at + 1))
                if (m[at] == 24) {
                    uint8_t next[32];
                    hkdf_expand_label(t->server.secret, "traffic upd", 0, 0, next, 32);
                    set_keys13(t, 1, next);
                }
        }
    }
    unsigned int part = t->pending_length < capacity ? t->pending_length : capacity;
    memcpy(data, t->pending, part);
    t->pending += part; t->pending_length -= part;
    if (!t->pending_length) t->pending = 0;
    return (int)part;
}

void tls_close(Tls *t)
{
    if (t->state == TLS_OPEN) {
        static const uint8_t close_notify[2] = {1, 0};
        seal_record(t, ALERT, close_notify, 2);
    }
    t->state = TLS_CLOSED;
}
