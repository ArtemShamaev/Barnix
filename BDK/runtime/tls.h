/* Barnix Net TLS 1.2/1.3 client state (see tls.c). */
#ifndef BNET_TLS_H
#define BNET_TLS_H
#include "bnet.h"
#define TLS_CERTS 8
enum { TLS_FAILED = -1, TLS_START, TLS_ENCRYPTED, TLS_OPEN, TLS_CLOSED };
typedef struct { uint8_t secret[32], key[32], iv[12]; uint64_t sequence; } TlsKeys;
typedef struct {
    /* transport, set before tls_connect: send returns bytes sent (<= 0 on
     * error); recv returns bytes, 0 at the end, -2 on timeout, -1 on error */
    void *context;
    int (*send)(void *context, const void *data, unsigned int length);
    int (*recv)(void *context, void *data, unsigned int capacity, unsigned int timeout_ms);
    const char *error;          /* why the connection failed */
    int state, cipher, version, group, server_encrypted;
    unsigned int key_length;
    uint8_t client_random[32], server_random[32], shared[32], handshake_secret[32];
    uint8_t x25519_private[32], x25519_public[32], p256_private[32], p256_public_key[65];
    Sha256 transcript;
    TlsKeys client, server;
    Cert certs[TLS_CERTS];
    int cert_count;
    uint8_t *plain, *pending;
    unsigned int plain_length, pending_length, record_length, hs_length;
    uint8_t record[5 + 16384 + 256 + 16];
    uint8_t out[5 + 16384 + 256 + 32];
    uint8_t hs[32768];
    uint8_t chain[32768];
} Tls;
/* random: 32 bytes client random, 32 session id, 32 X25519 private key.
 * now: "YYYYMMDDHHMMSS" UTC or 0. Returns 0, or -1 with t->error. */
int tls_connect(Tls *t, const char *host, const uint8_t random[96], const uint8_t *roots, size_t roots_length,
                const char *now);
int tls_write(Tls *t, const void *data, unsigned int length);
/* Bytes read, 0 at the end of the stream, -1 on error. */
int tls_read(Tls *t, void *data, unsigned int capacity, unsigned int timeout_ms);
void tls_close(Tls *t);
#endif
