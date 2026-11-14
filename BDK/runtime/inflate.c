/* Barnix Net inflate: RFC 1951 deflate and the RFC 1950 zlib wrapper,
 * in the manner of Mark Adler's puff.c (canonical Huffman decoding bit by
 * bit). It reports how much input it used, which git packs need. */
#include <barnix_net.h>
#include "bnet.h"

typedef struct {
    const uint8_t *in; size_t length, at;
    uint32_t bits; int count;
    uint8_t *out; size_t capacity, n;
    int error;
} State;
typedef struct { short count[16], symbol[288]; } Huffman;

static int bits(State *s, int need)
{
    uint32_t value = s->bits;
    while (s->count < need) {
        if (s->at >= s->length) { s->error = 1; return 0; }
        value |= (uint32_t)s->in[s->at++] << s->count;
        s->count += 8;
    }
    s->bits = value >> need;
    s->count -= need;
    return (int)(value & ((1U << need) - 1));
}
static int decode(State *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int length = 1; length < 16; length++) {
        code |= bits(s, 1);
        if (s->error) return -1;
        int count = h->count[length];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count;
        first <<= 1; code <<= 1;
    }
    s->error = 1;
    return -1;
}
/* 0 complete, >0 incomplete (allowed for single codes), <0 oversubscribed. */
static int build(Huffman *h, const short *length, int n)
{
    short offsets[16];
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return left; }
    offsets[1] = 0;
    for (int i = 1; i < 15; i++) offsets[i + 1] = (short)(offsets[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (length[i]) h->symbol[offsets[length[i]]++] = (short)i;
    return left;
}
static int codes(State *s, const Huffman *lengths, const Huffman *distances)
{
    static const short base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = decode(s, lengths);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->n >= s->capacity) return -1;
            s->out[s->n++] = (uint8_t)symbol;
        } else if (symbol == 256) return 0;
        else {
            symbol -= 257;
            if (symbol >= 29) return -1;
            size_t length = (size_t)(base[symbol] + bits(s, extra[symbol]));
            int d = decode(s, distances);
            if (d < 0 || d >= 30) return -1;
            size_t distance = (size_t)(dbase[d] + bits(s, dextra[d]));
            if (s->error || distance > s->n || s->n + length > s->capacity) return -1;
            for (; length; length--, s->n++) s->out[s->n] = s->out[s->n - distance];
        }
    }
}
static int stored(State *s)
{
    s->bits = 0; s->count = 0;
    if (s->at + 4 > s->length) return -1;
    size_t length = s->in[s->at] | (size_t)s->in[s->at + 1] << 8;
    if ((size_t)(s->in[s->at + 2] | s->in[s->at + 3] << 8) != (~length & 0xffff)) return -1;
    s->at += 4;
    if (s->at + length > s->length || s->n + length > s->capacity) return -1;
    memcpy(s->out + s->n, s->in + s->at, length);
    s->at += length; s->n += length;
    return 0;
}
static int fixed(State *s)
{
    static Huffman lengths, distances;
    static int ready;
    if (!ready) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lengths, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&distances, l, 30);
        ready = 1;
    }
    return codes(s, &lengths, &distances);
}
static int dynamic(State *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short l[320];
    Huffman lengths, distances;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30) return -1;
    int i = 0;
    for (; i < ncode; i++) l[order[i]] = (short)bits(s, 3);
    for (; i < 19; i++) l[order[i]] = 0;
    if (build(&lengths, l, 19)) return -1;
    for (i = 0; i < nlen + ndist;) {
        int symbol = decode(s, &lengths);
        if (symbol < 0) return -1;
        if (symbol < 16) { l[i++] = (short)symbol; continue; }
        short value = 0;
        int repeat;
        if (symbol == 16) { if (!i) return -1; value = l[i - 1]; repeat = 3 + bits(s, 2); }
        else if (symbol == 17) repeat = 3 + bits(s, 3);
        else repeat = 11 + bits(s, 7);
        if (s->error || i + repeat > nlen + ndist) return -1;
        while (repeat--) l[i++] = value;
    }
    if (!l[256]) return -1;
    int err = build(&lengths, l, nlen);
    if (err < 0 || (err > 0 && nlen - lengths.count[0] != 1)) return -1;
    err = build(&distances, l + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distances.count[0] != 1)) return -1;
    return codes(s, &lengths, &distances);
}
long inflate_raw(const uint8_t *in, size_t in_length, size_t *used, uint8_t *out, size_t capacity)
{
    State s = {in, in_length, 0, 0, 0, out, capacity, 0, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.error) return -1;
        int result = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (result || s.error) return -1;
    } while (!last);
    if (used) *used = s.at;
    return (long)s.n;
}
long inflate_zlib(const uint8_t *in, size_t in_length, size_t *used, uint8_t *out, size_t capacity)
{
    if (in_length < 6 || (in[0] & 15) != 8 || (in[1] & 0x20) || ((unsigned int)in[0] << 8 | in[1]) % 31) return -1;
    size_t raw_used;
    long n = inflate_raw(in + 2, in_length - 2, &raw_used, out, capacity);
    if (n < 0 || 2 + raw_used + 4 > in_length) return -1;
    uint32_t a = 1, b = 0;
    for (long i = 0; i < n; i++) { a += out[i]; if (a >= 65521) a -= 65521; b += a; if (b >= 65521) b -= 65521; }
    const uint8_t *check = in + 2 + raw_used;
    if (((uint32_t)check[0] << 24 | (uint32_t)check[1] << 16 | (uint32_t)check[2] << 8 | check[3]) != (b << 16 | a)) return -1;
    if (used) *used = 2 + raw_used + 4;
    return n;
}
long bnet_inflate(const void *in, unsigned long in_length, unsigned long *used, void *out, unsigned long capacity, int zlib)
{
    size_t consumed = 0;
    long result = zlib ? inflate_zlib(in, in_length, &consumed, out, capacity) : inflate_raw(in, in_length, &consumed, out, capacity);
    if (used) *used = consumed;
    return result;
}

/* gzip (RFC 1952): a header, raw deflate, CRC-32 and the size modulo 2^32. */
static long gzip_start(const uint8_t *p, unsigned long length)
{
    if (length < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) return -1;
    unsigned int flags = p[3];
    unsigned long at = 10;
    if (flags & 4) { if (at + 2 > length) return -1; at += 2 + (p[at] | (unsigned long)p[at + 1] << 8); }
    if (flags & 8) { while (at < length && p[at]) at++; at++; }
    if (flags & 16) { while (at < length && p[at]) at++; at++; }
    if (flags & 2) at += 2;
    return at + 8 <= length ? (long)at : -1;
}
long bnet_gunzip_size(const void *in, unsigned long in_length)
{
    const uint8_t *p = in;
    if (gzip_start(p, in_length) < 0) return -1;
    p += in_length - 4;
    return (long)(p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24);
}
long bnet_gunzip(const void *in, unsigned long in_length, void *out, unsigned long capacity)
{
    long at = gzip_start(in, in_length);
    if (at < 0) return -1;
    return bnet_inflate((const uint8_t *)in + at, in_length - (unsigned long)at - 8, 0, out, capacity, 0);
}
