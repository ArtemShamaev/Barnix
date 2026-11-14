/* WPA2-Personal cryptography for the Wi-Fi layer: SHA-1 and HMAC-SHA1,
 * PBKDF2 (passphrase -> PMK), the 802.11 PRF (PTK), AES-128 both ways,
 * AES key unwrap (RFC 3394, the GTK) and CCMP (AES-CCM, 8-byte MIC).
 * Self-contained so host tests can check it against reference vectors. */
#ifndef BARNIX_WIFI_CRYPTO_H
#define BARNIX_WIFI_CRYPTO_H
#include <stdint.h>

typedef struct { uint32_t h[5]; uint8_t block[64]; uint32_t used; uint64_t bytes; } WifiSha1;
void wifi_sha1_init(WifiSha1 *s);
void wifi_sha1_update(WifiSha1 *s, const void *data, unsigned int length);
void wifi_sha1_final(WifiSha1 *s, uint8_t out[20]);
void wifi_hmac_sha1(const uint8_t *key, unsigned int key_length, const uint8_t *data, unsigned int length, uint8_t out[20]);
/* PMK = PBKDF2-HMAC-SHA1(passphrase, SSID, 4096, 32 bytes). */
void wifi_psk(const char *passphrase, const uint8_t *ssid, unsigned int ssid_length, uint8_t pmk[32]);
/* PRF-n of IEEE 802.11 (n = out_length * 8). */
void wifi_prf(const uint8_t *key, unsigned int key_length, const char *label,
              const uint8_t *data, unsigned int data_length, uint8_t *out, unsigned int out_length);
/* PTK = PRF-384(PMK, "Pairwise key expansion", min/max of the addresses and nonces):
 * KCK 16 | KEK 16 | TK 16. */
void wifi_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
              const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48]);

typedef struct { uint32_t enc[44], dec[44]; } WifiAes;
void wifi_aes_key(WifiAes *aes, const uint8_t key[16]);
void wifi_aes_encrypt(const WifiAes *aes, const uint8_t in[16], uint8_t out[16]);
void wifi_aes_decrypt(const WifiAes *aes, const uint8_t in[16], uint8_t out[16]);
/* RFC 3394: in is length bytes (a multiple of 8, >= 24), out gets length - 8.
 * 0 when the integrity check value matches. */
int wifi_aes_unwrap(const uint8_t kek[16], const uint8_t *in, unsigned int length, uint8_t *out);

/* CCMP over one MPDU. header: the 802.11 header (24, 26, 30 or 32 bytes).
 * Encrypt: data/length plaintext, writes ciphertext + 8-byte MIC to out.
 * Decrypt: data/length ciphertext with the MIC at its end, writes the
 * plaintext (length - 8) to out; 0 when the MIC is right. pn: 48-bit. */
void wifi_ccmp_encrypt(const WifiAes *tk, const uint8_t *header, unsigned int header_length,
                       uint64_t pn, const uint8_t *data, unsigned int length, uint8_t *out);
int wifi_ccmp_decrypt(const WifiAes *tk, const uint8_t *header, unsigned int header_length,
                      uint64_t pn, const uint8_t *data, unsigned int length, uint8_t *out);
#endif
