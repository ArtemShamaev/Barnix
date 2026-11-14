/* Barnix Net internals: cryptography, X.509, TLS 1.3, inflate. Shared by
 * BDK/runtime/{crypto,pk,x509,tls,http,inflate}.c; programs include
 * <barnix_net.h> instead. Freestanding: no division of 64-bit numbers. */
#ifndef BNET_H
#define BNET_H
#include <stddef.h>
#include <stdint.h>
void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
void *memmove(void *destination, const void *source, size_t size);
int memcmp(const void *a, const void *b, size_t size);
size_t strlen(const char *s);

/* ------------------------------------------------------------ hashes */
typedef struct { uint32_t h[5]; uint64_t length; uint8_t block[64]; unsigned int used; } Sha1;
typedef struct { uint32_t h[8]; uint64_t length; uint8_t block[64]; unsigned int used; } Sha256;
typedef struct { uint64_t h[8]; uint64_t length; uint8_t block[128]; unsigned int used, out; } Sha512;
void sha1_init(Sha1 *s);
void sha1_update(Sha1 *s, const void *data, size_t length);
void sha1_final(Sha1 *s, uint8_t out[20]);
void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t length);
void sha256_final(Sha256 *s, uint8_t out[32]);
void sha384_init(Sha512 *s);
void sha512_init(Sha512 *s);
void sha512_update(Sha512 *s, const void *data, size_t length);
void sha512_final(Sha512 *s, uint8_t *out);   /* 48 bytes for SHA-384, 64 for SHA-512 */
/* One-shot digests by algorithm: 1 SHA-1, 256, 384, 512. Returns the length. */
unsigned int hash_digest(int algorithm, const void *data, size_t length, uint8_t *out);

void hmac_sha256(const uint8_t *key, size_t key_length, const void *data, size_t length, uint8_t out[32]);
void hkdf_extract(const uint8_t *salt, size_t salt_length, const uint8_t *ikm, size_t ikm_length, uint8_t out[32]);
void hkdf_expand_label(const uint8_t secret[32], const char *label, const uint8_t *context, size_t context_length,
                       uint8_t *out, size_t length);

/* ------------------------------------------------------------ ciphers */
typedef struct { uint32_t round[44]; } Aes128;
void aes128_init(Aes128 *a, const uint8_t key[16]);
void aes128_block(const Aes128 *a, const uint8_t in[16], uint8_t out[16]);
/* AEAD: tag is 16 bytes after the ciphertext. open returns 0 when authentic. */
void aes_gcm_seal(const uint8_t key[16], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                  uint8_t *data, size_t length, uint8_t tag[16]);
int aes_gcm_open(const uint8_t key[16], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                 uint8_t *data, size_t length, const uint8_t tag[16]);
void chacha_poly_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                      uint8_t *data, size_t length, uint8_t tag[16]);
int chacha_poly_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aad_length,
                     uint8_t *data, size_t length, const uint8_t tag[16]);
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

/* ------------------------------------------------------------ public keys */
/* RSA: modulus and exponent big-endian. hash: 1, 256, 384 or 512. */
int rsa_pkcs1_verify(const uint8_t *n, size_t n_length, const uint8_t *e, size_t e_length,
                     int hash, const uint8_t *digest, const uint8_t *signature, size_t signature_length);
int rsa_pss_verify(const uint8_t *n, size_t n_length, const uint8_t *e, size_t e_length,
                   int hash, const uint8_t *digest, const uint8_t *signature, size_t signature_length);
/* ECDSA: curve 256 or 384, point uncompressed (04 X Y), signature DER. */
int ecdsa_verify(int curve, const uint8_t *point, size_t point_length, const uint8_t *digest, size_t digest_length,
                 const uint8_t *signature, size_t signature_length);

/* ECDH on P-256: private_key is 32 random bytes (adjusted in place). */
int p256_public(uint8_t private_key[32], uint8_t public_key[65]);
int p256_shared(const uint8_t private_key[32], const uint8_t *peer, size_t peer_length, uint8_t shared[32]);

/* ------------------------------------------------------------ DER and X.509 */
typedef struct { const uint8_t *p; size_t n; } Der;
/* Reads one element of tag from d (advancing it) into out (contents). */
int der_get(Der *d, int tag, Der *out);
int der_peek(const Der *d);

typedef struct {
    Der raw, tbs, issuer, subject, spki, key, signature, san;
    int key_type;              /* 1 RSA, 256/384 EC curve */
    Der rsa_n, rsa_e;
    int sig_hash, sig_kind;    /* hash 1/256/384/512; kind 1 RSA PKCS#1, 2 ECDSA, 3 RSA-PSS */
    int ca;
    uint8_t not_before[14], not_after[14];   /* YYYYMMDDHHMMSS */
} Cert;
int cert_parse(const uint8_t *data, size_t length, Cert *c);
/* Verifies that issuer signed c. */
int cert_signed_by(const Cert *c, const Cert *issuer);
int cert_hostname(const Cert *c, const char *host);
/* Chain: certs[0] leaf; the trust store is the Barnix roots file. now is
 * "YYYYMMDDHHMMSS" UTC (0 skips the dates). Returns 0 or an error text. */
const char *chain_verify(Cert *certs, int count, const char *host, const char *now,
                         const uint8_t *roots, size_t roots_length);

/* ------------------------------------------------------------ platform */
/* Supplied by net.c on Barnix (and by test programs elsewhere). open
 * returns a connection handle or -1 with error filled; recv returns bytes,
 * 0 at the end, -2 on timeout, -1 on error. */
int bnet_platform_open(const char *host, unsigned int port, char *error, unsigned int error_capacity);
int bnet_platform_send(int handle, const void *data, unsigned int length);
int bnet_platform_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms);
void bnet_platform_close(int handle);
void bnet_platform_random(void *out, unsigned int length);
const uint8_t *bnet_platform_roots(size_t *length);
int bnet_platform_now(char out[15]);   /* "YYYYMMDDHHMMSS", -1 when unknown */

/* ------------------------------------------------------------ inflate */
/* Raw deflate. Returns bytes written or -1; *used gets the input consumed. */
long inflate_raw(const uint8_t *in, size_t in_length, size_t *used, uint8_t *out, size_t capacity);
/* zlib wrapper (2-byte header, Adler-32). */
long inflate_zlib(const uint8_t *in, size_t in_length, size_t *used, uint8_t *out, size_t capacity);
#endif
