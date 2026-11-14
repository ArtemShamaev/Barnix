#include "sha256.h"
#include "barnix.h"

static unsigned int rotate(unsigned int value, int n) { return value >> n | value << (32 - n); }
static unsigned int big32(const unsigned char *p) {
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}
static void put32(unsigned char *p, unsigned int value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static const unsigned int round_constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void block(SetupSha256 *hash, const unsigned char *input)
{
    unsigned int words[64], work[8];
    for (int i = 0; i < 16; i++) words[i] = big32(input + 4 * i);
    for (int i = 16; i < 64; i++) {
        unsigned int s0 = rotate(words[i-15],7) ^ rotate(words[i-15],18) ^ (words[i-15] >> 3);
        unsigned int s1 = rotate(words[i-2],17) ^ rotate(words[i-2],19) ^ (words[i-2] >> 10);
        words[i] = words[i-16] + s0 + words[i-7] + s1;
    }
    memcpy(work, hash->state, sizeof(work));
    for (int i = 0; i < 64; i++) {
        unsigned int s1 = rotate(work[4],6) ^ rotate(work[4],11) ^ rotate(work[4],25);
        unsigned int a = work[7] + s1 + ((work[4]&work[5]) ^ (~work[4]&work[6])) + round_constants[i] + words[i];
        unsigned int s0 = rotate(work[0],2) ^ rotate(work[0],13) ^ rotate(work[0],22);
        unsigned int b = s0 + ((work[0]&work[1]) ^ (work[0]&work[2]) ^ (work[1]&work[2]));
        work[7]=work[6]; work[6]=work[5]; work[5]=work[4]; work[4]=work[3]+a;
        work[3]=work[2]; work[2]=work[1]; work[1]=work[0]; work[0]=a+b;
    }
    for (int i = 0; i < 8; i++) hash->state[i] += work[i];
}
void setup_sha256_init(SetupSha256 *hash)
{
    static const unsigned int iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                       0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(hash->state,iv,sizeof(iv)); hash->length=0; hash->used=0;
}
void setup_sha256_update(SetupSha256 *hash, const void *bytes, unsigned int length)
{
    const unsigned char *input=bytes; hash->length += length;
    while (length) {
        unsigned int count = 64 - hash->used;
        if (count > length) count = length;
        memcpy(hash->block + hash->used, input, count);
        hash->used += count; input += count; length -= count;
        if (hash->used == 64) { block(hash, hash->block); hash->used = 0; }
    }
}
void setup_sha256_final(SetupSha256 *hash, unsigned char output[32])
{
    unsigned long long bits = hash->length * 8;
    unsigned char one = 0x80, zero = 0;
    setup_sha256_update(hash, &one, 1);
    while (hash->used != 56) setup_sha256_update(hash, &zero, 1);
    unsigned char length[8];
    for (int i = 0; i < 8; i++) length[7-i] = bits >> (8*i);
    setup_sha256_update(hash, length, 8);
    for (int i = 0; i < 8; i++) put32(output + 4*i, hash->state[i]);
}
