#include "barnix.h"
#include "password.h"
static unsigned int rotate(unsigned int x, int n) { return (x >> n) | (x << (32-n)); }
static void sha256(const unsigned char *data, unsigned int size, unsigned char out[32]) {
    static const unsigned int k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    unsigned int h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned int blocks = (size + 9 + 63) / 64;
    for (unsigned int block = 0; block < blocks; block++) {
        unsigned int w[64] = {0};
        for (unsigned int i = 0; i < 64; i++) {
            unsigned int p = block*64+i; unsigned char byte = p < size ? data[p] : p == size ? 128 : 0;
            if (p >= blocks*64-4) byte = (size*8) >> ((blocks*64-1-p)*8);
            w[i/4] |= (unsigned int)byte << (24-(i%4)*8);
        }
        for (int i = 16; i < 64; i++) {
            unsigned int a=w[i-15], b=w[i-2];
            w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
        }
        unsigned int a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for (int i=0;i<64;i++) {
            unsigned int t=v+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            unsigned int u=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    for (int i=0;i<32;i++) out[i]=h[i/4]>>(24-(i%4)*8);
}
void password_encode(const char *password, const char *salt, char out[96]) {
    unsigned char buffer[128], digest[32];
    int n=strlen(password); if(n>63)n=63;
    memcpy(buffer,salt,16); memcpy(buffer+16,password,n); sha256(buffer,n+16,digest);
    for(int i=0;i<4096;i++) { memcpy(buffer,digest,32); memcpy(buffer+32,salt,16); memcpy(buffer+48,password,n); sha256(buffer,n+48,digest); }
    strcpy(out,"$bx1$"); memcpy(out+5,salt,16); out[21]='$';
    for(int i=0;i<32;i++) { out[22+i*2]="0123456789abcdef"[digest[i]>>4]; out[23+i*2]="0123456789abcdef"[digest[i]&15]; }
    out[86]=0; memset(buffer,0,sizeof(buffer)); memset(digest,0,sizeof(digest));
}
int password_matches(const char *password,const char *encoded) {
    if(strncmp(encoded,"$bx1$",5)) return *encoded && !strcmp(password,encoded);
    if(strlen(encoded)!=86 || encoded[21]!='$' || strlen(password)>63) return 0;
    char hash[96]; password_encode(password,encoded+5,hash);
    unsigned int diff=0; for(int i=0;i<86;i++)diff|=(unsigned char)hash[i]^(unsigned char)encoded[i];
    memset(hash,0,sizeof(hash)); return !diff;
}
