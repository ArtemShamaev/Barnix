/* Gouse Web: PNG images (ISO/IEC 15948): every color type and bit depth,
 * transparency (tRNS) and Adam7 interlacing, inflated by web_inflate. */
#include "web.h"

static unsigned int be32(const unsigned char *p) { return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3]; }
static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
static int unfilter(unsigned char *rows, int height, int stride, int bpp)
{
    unsigned char *prior = 0;
    for (int y = 0; y < height; y++) {
        unsigned char *line = rows + y * (stride + 1);
        int type = line[0];
        unsigned char *p = line + 1;
        for (int i = 0; i < stride; i++) {
            int a = i >= bpp ? p[i - bpp] : 0, b = prior ? prior[i] : 0, c = prior && i >= bpp ? prior[i - bpp] : 0;
            switch (type) {
            case 0: break;
            case 1: p[i] = (unsigned char)(p[i] + a); break;
            case 2: p[i] = (unsigned char)(p[i] + b); break;
            case 3: p[i] = (unsigned char)(p[i] + (a + b) / 2); break;
            case 4: p[i] = (unsigned char)(p[i] + paeth(a, b, c)); break;
            default: return -1;
            }
        }
        prior = p;
    }
    return 0;
}
typedef struct {
    int depth, color, channels;
    unsigned int palette[256];
    int trns_gray, trns_r, trns_g, trns_b, has_trns;
} Format;
static unsigned int sample(const Format *f, const unsigned char *row, int x)
{
    int d = f->depth;
    unsigned int v[4];
    for (int c = 0; c < f->channels; c++) {
        if (d == 8) v[c] = row[x * f->channels + c];
        else if (d == 16) v[c] = row[(x * f->channels + c) * 2] << 8 | row[(x * f->channels + c) * 2 + 1];
        else { int bit = (x * f->channels + c) * d; v[c] = (unsigned)(row[bit >> 3] >> (8 - d - (bit & 7))) & ((1u << d) - 1); }
    }
    unsigned int max = (1u << d) - 1;
#define TO8(x) (d == 8 ? (x) : d == 16 ? (x) >> 8 : (x) * 255 / max)
    switch (f->color) {
    case 0: { unsigned int g = TO8(v[0]); unsigned int a = f->has_trns && (int)v[0] == f->trns_gray ? 0 : 255; return a << 24 | g << 16 | g << 8 | g; }
    case 2: { unsigned int a = f->has_trns && (int)v[0] == f->trns_r && (int)v[1] == f->trns_g && (int)v[2] == f->trns_b ? 0 : 255;
              return a << 24 | TO8(v[0]) << 16 | TO8(v[1]) << 8 | TO8(v[2]); }
    case 3: return f->palette[v[0] & 255];
    case 4: { unsigned int g = TO8(v[0]); return TO8(v[1]) << 24 | g << 16 | g << 8 | g; }
    case 6: return TO8(v[3]) << 24 | TO8(v[0]) << 16 | TO8(v[1]) << 8 | TO8(v[2]);
    }
#undef TO8
    return 0;
}
WImage *web_png(const unsigned char *data, int length)
{
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (length < 33) return 0;
    for (int i = 0; i < 8; i++) if (data[i] != signature[i]) return 0;
    Format f;
    for (int i = 0; i < 256; i++) f.palette[i] = 0xff000000u;
    f.depth = f.color = 0;
    f.has_trns = 0; f.trns_gray = f.trns_r = f.trns_g = f.trns_b = -1;
    int width = 0, height = 0, interlace = 0;
    unsigned int idat_total = 0;
    for (int at = 8; at + 12 <= length;) {   /* first pass: sizes */
        unsigned int size = be32(data + at);
        const unsigned char *type = data + at + 4, *body = data + at + 8;
        if (size > (unsigned)length || at + 12 + (int)size > length) break;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R' && size >= 13) {
            width = (int)be32(body); height = (int)be32(body + 4);
            f.depth = body[8]; f.color = body[9]; interlace = body[12];
        } else if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E') {
            for (unsigned int i = 0; i < size / 3 && i < 256; i++) f.palette[i] = 0xff000000u | (unsigned)body[i * 3] << 16 | (unsigned)body[i * 3 + 1] << 8 | body[i * 3 + 2];
        } else if (type[0] == 't' && type[1] == 'R' && type[2] == 'N' && type[3] == 'S') {
            f.has_trns = 1;
            if (f.color == 3) for (unsigned int i = 0; i < size && i < 256; i++) f.palette[i] = (f.palette[i] & 0xffffff) | (unsigned)body[i] << 24;
            else if (f.color == 0 && size >= 2) f.trns_gray = body[0] << 8 | body[1];
            else if (f.color == 2 && size >= 6) { f.trns_r = body[0] << 8 | body[1]; f.trns_g = body[2] << 8 | body[3]; f.trns_b = body[4] << 8 | body[5]; }
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') idat_total += size;
        else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') break;
        at += 12 + (int)size;
    }
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || (long long)width * height > 4 * 1024 * 1024 || !idat_total) return 0;
    if (f.depth == 8 || f.depth == 16) {
        if (f.depth == 16 && f.color == 3) return 0;
    } else if (f.depth != 1 && f.depth != 2 && f.depth != 4) return 0;
    f.channels = f.color == 0 ? 1 : f.color == 2 ? 3 : f.color == 3 ? 1 : f.color == 4 ? 2 : f.color == 6 ? 4 : 0;
    if (!f.channels) return 0;
    unsigned char *compressed = web_alloc_big(idat_total);
    if (!compressed) return 0;
    unsigned int n = 0;
    for (int at = 8; at + 12 <= length;) {
        unsigned int size = be32(data + at);
        const unsigned char *type = data + at + 4;
        if (at + 12 + (int)size > length) break;
        if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') { for (unsigned int i = 0; i < size; i++) compressed[n++] = data[at + 8 + i]; }
        at += 12 + (int)size;
    }
    int bits = f.channels * f.depth, bpp = bits >= 8 ? bits / 8 : 1;
    static const int x0[7] = {0, 4, 0, 2, 0, 1, 0}, y0[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    unsigned int raw_size = 0;
    for (int pass = 0; pass < (interlace ? 7 : 1); pass++) {
        int pw = interlace ? (width - x0[pass] + dx[pass] - 1) / dx[pass] : width, ph = interlace ? (height - y0[pass] + dy[pass] - 1) / dy[pass] : height;
        if (pw > 0 && ph > 0) raw_size += (unsigned)ph * ((unsigned)(pw * bits + 7) / 8 + 1);
    }
    unsigned char *raw = web_alloc_big(raw_size + 16);
    WImage *image = web_alloc(sizeof(WImage));
    image->pixels = web_alloc_big((unsigned)width * (unsigned)height * 4);
    if (!raw || !image->pixels) return 0;
    long got = web_inflate(compressed, n, raw, raw_size + 16, 1);
    if (got < (long)raw_size) return 0;
    image->width = width; image->height = height;
    unsigned char *p = raw;
    for (int pass = 0; pass < (interlace ? 7 : 1); pass++) {
        int pw = interlace ? (width - x0[pass] + dx[pass] - 1) / dx[pass] : width, ph = interlace ? (height - y0[pass] + dy[pass] - 1) / dy[pass] : height;
        if (pw <= 0 || ph <= 0) continue;
        int stride = (pw * bits + 7) / 8;
        if (unfilter(p, ph, stride, bpp)) return 0;
        for (int y = 0; y < ph; y++) {
            const unsigned char *row = p + y * (stride + 1) + 1;
            int ty = interlace ? y0[pass] + y * dy[pass] : y;
            for (int x = 0; x < pw; x++) {
                int tx = interlace ? x0[pass] + x * dx[pass] : x;
                image->pixels[ty * width + tx] = sample(&f, row, x);
            }
        }
        p += ph * (stride + 1);
    }
    return image;
}
