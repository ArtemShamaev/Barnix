#ifndef BARNIX_SETUP_SHA256_H
#define BARNIX_SETUP_SHA256_H
typedef struct {
    unsigned int state[8];
    unsigned long long length;
    unsigned char block[64];
    unsigned int used;
} SetupSha256;
void setup_sha256_init(SetupSha256 *hash);
void setup_sha256_update(SetupSha256 *hash, const void *bytes, unsigned int length);
void setup_sha256_final(SetupSha256 *hash, unsigned char output[32]);
#endif
