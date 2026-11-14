/* Natives for files, zip (inflate, a fixed-Huffman LZ77 deflater, CRC-32),
 * PNG images and raw memory access behind java.nio buffers. */
#include "jvm.h"
#include <stdio.h>

long bnet_inflate(const void *in, unsigned long in_length, unsigned long *used, void *out, unsigned long capacity, int zlib);

#define N(name) static void name(Thread *t, Slot *a)

static char *path_arg(Thread *t, Object *s, char *buffer, uint32_t size)
{
    if (!s) { throw_npe(t); return 0; }
    return string_utf8(s, buffer, size);
}

/* ---- java.io.File ---- */
N(file_exists) { char p[512]; if (!path_arg(t, a[0].l, p, sizeof(p))) return; a[0].i = plat_is_dir(p) || plat_file_size(p) >= 0; }
N(file_is_dir) { char p[512]; if (!path_arg(t, a[0].l, p, sizeof(p))) return; a[0].i = plat_is_dir(p); }
N(file_length)
{
    char p[512];
    if (!path_arg(t, a[0].l, p, sizeof(p))) return;
    int n = plat_is_dir(p) ? 0 : plat_file_size(p);
    put_long(a, n < 0 ? 0 : n);
}
N(file_list)
{
    char p[512], name[256];
    if (!path_arg(t, a[0].l, p, sizeof(p))) return;
    a[0].l = 0;
    if (!plat_is_dir(p)) return;
    uint32_t position = 0, n = 0;
    while (plat_list(p, &position, name, sizeof(name)) > 0) if (strcmp(name, ".") && strcmp(name, "..")) n++;
    Array *r = new_array(t, vm.string_array, (jint)n);
    if (!r) return;
    a[0].l = &r->o;
    position = 0;
    uint32_t i = 0;
    while (i < n && plat_list(p, &position, name, sizeof(name)) > 0) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        Object *s = new_string(t, name);
        if (!s) return;
        ADATA(r, Object *)[i++] = s;
    }
    r->length = (jint)i;
}
N(file_mkdir) { char p[512]; if (!path_arg(t, a[0].l, p, sizeof(p))) return; a[0].i = !plat_is_dir(p) && plat_file_size(p) < 0 && plat_mkdir(p) == 0; }
N(file_delete) { char p[512]; if (!path_arg(t, a[0].l, p, sizeof(p))) return; a[0].i = plat_delete(p) == 0; }
N(file_rename)
{
    char p[512], q[512];
    if (!path_arg(t, a[0].l, p, sizeof(p)) || !path_arg(t, a[1].l, q, sizeof(q))) return;
    a[0].i = plat_rename(p, q) == 0;
}
N(file_cwd) { char p[512] = "/"; plat_getcwd(p, sizeof(p)); a[0].l = new_string(t, p); }

/* ---- streams: stateless reads and writes by path ---- */
N(fis_read)
{
    char p[512];
    if (!path_arg(t, a[0].l, p, sizeof(p))) return;
    jlong position = get_long(a + 1);
    Array *b = (Array *)a[3].l;
    jint off = a[4].i, n = a[5].i;
    if (!b) { throw_npe(t); return; }
    if (off < 0 || n < 0 || off > b->length - n) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    int size = plat_file_size(p);
    if (size < 0) { throw_new(t, "java/io/IOException", "file vanished"); return; }
    if (position >= size) { a[0].i = -1; return; }
    if (n > size - position) n = (jint)(size - position);
    int got = n ? plat_file_read(p, (uint32_t)position, ADATA(b, uint8_t) + off, (uint32_t)n) : 0;
    if (got < 0) { throw_new(t, "java/io/IOException", "read error"); return; }
    a[0].i = got;
}
N(fos_write)
{
    char p[512];
    if (!path_arg(t, a[0].l, p, sizeof(p))) return;
    Array *b = (Array *)a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (b && (off < 0 || n < 0 || off > b->length - n)) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    a[0].i = plat_file_write(p, b ? ADATA(b, uint8_t) + off : (const uint8_t *)"", b ? (uint32_t)n : 0, a[4].i) == 0;
}

/* ---- java.util.zip ---- */
N(inflater_inflate)
{
    Array *in = (Array *)a[0].l;
    jint off = a[1].i, n = a[2].i, raw = a[3].i;
    a[0].l = 0;
    if (!in || off < 0 || n < 0 || off > in->length - n) return;
    uint32_t capacity = (uint32_t)n * 4 + 1024;
    for (int attempt = 0; attempt < 8; attempt++, capacity *= 4) {
        uint8_t *out = vm_malloc(capacity);
        if (!out) return;
        unsigned long used = 0;
        long got = bnet_inflate(ADATA(in, uint8_t) + off, (unsigned long)n, &used, out, capacity, !raw);
        if (got >= 0 && (uint32_t)got < capacity) {
            Array *r = new_prim_array(t, T_BYTE, (jint)got + 4);
            if (r) {
                memcpy(ADATA(r, uint8_t) + 4, out, (size_t)got);
                memcpy(ADATA(r, uint8_t), &(jint){(jint)used}, 4);
                r->length = (jint)got + 4;
            }
            vm_free(out);
            a[0].l = r ? &r->o : 0;
            return;
        }
        vm_free(out);
        if (got >= 0) continue;
        /* -1: corrupt, or the output did not fit; retry larger, then give up. */
    }
}

/* Deflate: one final block of fixed Huffman codes, LZ77 with hash chains. */
typedef struct { uint8_t *out; uint32_t capacity, at, bits, count; } Bits;
static void put_bits(Bits *b, uint32_t value, uint32_t n)
{
    b->bits |= value << b->count;
    b->count += n;
    while (b->count >= 8) {
        if (b->at < b->capacity) b->out[b->at] = (uint8_t)b->bits;
        b->at++;
        b->bits >>= 8;
        b->count -= 8;
    }
}
static uint32_t reverse(uint32_t v, uint32_t n) { uint32_t r = 0; while (n--) { r = r << 1 | (v & 1); v >>= 1; } return r; }
static void put_literal(Bits *b, uint32_t v)
{
    if (v < 144) put_bits(b, reverse(0x30 + v, 8), 8);
    else if (v < 256) put_bits(b, reverse(0x190 + v - 144, 9), 9);
    else if (v < 280) put_bits(b, reverse(v - 256, 7), 7);
    else put_bits(b, reverse(0xc0 + v - 280, 8), 8);
}
static const uint16_t length_base[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

uint32_t bjvm_deflate(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t capacity)
{
    enum { HASH = 1 << 15, WINDOW = 32768, CHAIN = 16 };
    int32_t *head = vm_malloc(HASH * 4), *prev = vm_malloc(WINDOW * 4);
    Bits b = {out, capacity, 0, 0, 0};
    put_bits(&b, 1, 1);
    put_bits(&b, 1, 2);
    if (head && prev) {
        memset(head, 0xff, HASH * 4);
        uint32_t i = 0;
        while (i < n) {
            uint32_t best = 0, best_dist = 0;
            if (i + 3 <= n) {
                uint32_t h = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & (HASH - 1);
                int32_t c = head[h];
                for (int k = 0; k < CHAIN && c >= 0 && i - (uint32_t)c <= WINDOW; k++) {
                    uint32_t l = 0, max = n - i < 258 ? n - i : 258;
                    while (l < max && in[c + l] == in[i + l]) l++;
                    if (l > best) { best = l; best_dist = i - (uint32_t)c; if (l == max) break; }
                    c = prev[c & (WINDOW - 1)];
                }
                prev[i & (WINDOW - 1)] = head[h];
                head[h] = (int32_t)i;
            }
            if (best >= 3) {
                uint32_t code = 0;
                while (code < 28 && length_base[code + 1] <= best) code++;
                put_literal(&b, 257 + code);
                put_bits(&b, best - length_base[code], length_extra[code]);
                uint32_t d = 0;
                while (d < 29 && dist_base[d + 1] <= best_dist) d++;
                put_bits(&b, reverse(d, 5), 5);
                put_bits(&b, best_dist - dist_base[d], dist_extra[d]);
                for (uint32_t k = 1; k < best; k++) {
                    uint32_t j = i + k;
                    if (j + 3 <= n) {
                        uint32_t h = ((uint32_t)in[j] << 10 ^ (uint32_t)in[j + 1] << 5 ^ in[j + 2]) & (HASH - 1);
                        prev[j & (WINDOW - 1)] = head[h];
                        head[h] = (int32_t)j;
                    }
                }
                i += best;
            } else put_literal(&b, in[i++]);
        }
    } else for (uint32_t i = 0; i < n; i++) put_literal(&b, in[i]);
    put_literal(&b, 256);
    put_bits(&b, 0, 7);
    vm_free(head);
    vm_free(prev);
    return b.at <= capacity ? b.at : 0;
}

N(deflater_deflate)
{
    Array *in = (Array *)a[0].l;
    jint off = a[1].i, n = a[2].i;
    a[0].l = 0;
    if (!in || off < 0 || n < 0 || off > in->length - n) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    uint32_t capacity = (uint32_t)n + (uint32_t)n / 4 + 64;
    uint8_t *out = vm_malloc(capacity);
    if (!out) { throw_new(t, "java/lang/OutOfMemoryError", "deflate"); return; }
    uint32_t got = bjvm_deflate(ADATA(in, uint8_t) + off, (uint32_t)n, out, capacity);
    Array *r = new_prim_array(t, T_BYTE, (jint)got);
    if (r) memcpy(ADATA(r, uint8_t), out, got);
    vm_free(out);
    a[0].l = r ? &r->o : 0;
}

static uint32_t crc_table[256];
uint32_t bjvm_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320U ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}
N(crc_update)
{
    Array *b = (Array *)a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (!b || off < 0 || n < 0 || off > b->length - n) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    a[0].i = (jint)bjvm_crc32((uint32_t)a[0].i, ADATA(b, uint8_t) + off, (uint32_t)n);
}

/* ---- PNG ---- */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Returns ARGB pixels (vm_malloc) or null. */
static uint32_t *png_decode(const uint8_t *d, uint32_t n, uint32_t *width, uint32_t *height)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n < 33 || memcmp(d, sig, 8)) return 0;
    uint32_t w = 0, h = 0, depth = 0, type = 0, interlace = 0, palette[256], trns_gray = 0x10000, trns_rgb[3] = {0x10000, 0, 0};
    uint32_t idat_size = 0;
    for (int i = 0; i < 256; i++) palette[i] = 0xff000000U;
    for (uint32_t at = 8; at + 12 <= n;) {
        uint32_t length = be32(d + at);
        const uint8_t *chunk = d + at + 4, *body = chunk + 4;
        if (at + 12 + length > n) break;
        if (!memcmp(chunk, "IHDR", 4) && length >= 13) {
            w = be32(body); h = be32(body + 4); depth = body[8]; type = body[9]; interlace = body[12];
        } else if (!memcmp(chunk, "PLTE", 4)) {
            for (uint32_t i = 0; i < length / 3 && i < 256; i++)
                palette[i] = 0xff000000U | (uint32_t)body[i * 3] << 16 | (uint32_t)body[i * 3 + 1] << 8 | body[i * 3 + 2];
        } else if (!memcmp(chunk, "tRNS", 4)) {
            if (type == 3) for (uint32_t i = 0; i < length && i < 256; i++) palette[i] = (palette[i] & 0xffffff) | (uint32_t)body[i] << 24;
            else if (type == 0 && length >= 2) trns_gray = (uint32_t)body[0] << 8 | body[1];
            else if (type == 2 && length >= 6)
                for (int k = 0; k < 3; k++) trns_rgb[k] = (uint32_t)body[k * 2] << 8 | body[k * 2 + 1];
        } else if (!memcmp(chunk, "IDAT", 4)) idat_size += length;
        else if (!memcmp(chunk, "IEND", 4)) break;
        at += 12 + length;
    }
    if (!w || !h || w > 16384 || h > 16384 || !idat_size) return 0;
    static const uint8_t channels[] = {1, 0, 3, 1, 2, 0, 4};
    if (type > 6 || !channels[type] || (depth != 8 && depth != 16 && !(depth < 8 && (type == 0 || type == 3)))) return 0;
    uint32_t bpp_bits = channels[type] * depth, bpp = (bpp_bits + 7) / 8;
    uint8_t *z = vm_malloc(idat_size);
    if (!z) return 0;
    uint32_t zn = 0;
    for (uint32_t at = 8; at + 12 <= n;) {
        uint32_t length = be32(d + at);
        if (at + 12 + length > n) break;
        if (!memcmp(d + at + 4, "IDAT", 4)) { memcpy(z + zn, d + at + 8, length); zn += length; }
        at += 12 + length;
    }
    /* Pass geometry (Adam7 or one pass). */
    static const uint8_t ax[7] = {0, 4, 0, 2, 0, 1, 0}, ay[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    int passes = interlace ? 7 : 1;
    uint32_t raw_size = 0;
    for (int p = 0; p < passes; p++) {
        uint32_t pw = interlace ? (w - ax[p] + dx[p] - 1) / dx[p] : w, ph = interlace ? (h - ay[p] + dy[p] - 1) / dy[p] : h;
        if (ax[p] >= w || ay[p] >= h) pw = ph = 0;
        if (pw && ph) raw_size += ph * (1 + (pw * bpp_bits + 7) / 8);
    }
    uint8_t *raw = vm_malloc(raw_size + 1);
    uint32_t *pixels = vm_malloc(w * h * 4);
    if (!raw || !pixels || bnet_inflate(z, zn, 0, raw, raw_size, 1) != (long)raw_size) {
        vm_free(z); vm_free(raw); vm_free(pixels);
        return 0;
    }
    vm_free(z);
    uint8_t *row = raw;
    for (int p = 0; p < passes; p++) {
        uint32_t pw = interlace ? (w - ax[p] + dx[p] - 1) / dx[p] : w, ph = interlace ? (h - ay[p] + dy[p] - 1) / dy[p] : h;
        if (!interlace) { pw = w; ph = h; } else if (ax[p] >= w || ay[p] >= h) continue;
        uint32_t stride = (pw * bpp_bits + 7) / 8;
        uint8_t *last = 0;
        for (uint32_t y = 0; y < ph; y++) {
            uint8_t filter = row[0], *cur = row + 1;
            for (uint32_t i = 0; i < stride; i++) {
                int left = i >= bpp ? cur[i - bpp] : 0, up = last ? last[i] : 0, ul = last && i >= bpp ? last[i - bpp] : 0;
                switch (filter) {
                case 1: cur[i] = (uint8_t)(cur[i] + left); break;
                case 2: cur[i] = (uint8_t)(cur[i] + up); break;
                case 3: cur[i] = (uint8_t)(cur[i] + ((left + up) >> 1)); break;
                case 4: cur[i] = (uint8_t)(cur[i] + paeth(left, up, ul)); break;
                }
            }
            for (uint32_t x = 0; x < pw; x++) {
                uint32_t argb;
                if (depth < 8) {
                    uint32_t bit = x * depth, v = cur[bit / 8] >> (8 - depth - bit % 8) & ((1U << depth) - 1);
                    if (type == 3) argb = palette[v];
                    else { uint32_t g = v * 255 / ((1U << depth) - 1); argb = (v == trns_gray ? 0 : 0xff000000U) | g << 16 | g << 8 | g; }
                } else {
                    const uint8_t *s = cur + x * bpp;
                    int step = depth == 16 ? 2 : 1;
                    uint32_t c0 = s[0], c1 = 0, c2 = 0, c3 = 255;
                    switch (type) {
                    case 0: c1 = c2 = c0; if ((depth == 16 ? (uint32_t)s[0] << 8 | s[1] : s[0]) == trns_gray) c3 = 0; break;
                    case 2: c1 = s[step]; c2 = s[step * 2];
                        if ((depth == 16 ? (uint32_t)s[0] << 8 | s[1] : s[0]) == trns_rgb[0] && (depth == 16 ? (uint32_t)s[2] << 8 | s[3] : s[1]) == trns_rgb[1] && (depth == 16 ? (uint32_t)s[4] << 8 | s[5] : s[2]) == trns_rgb[2]) c3 = 0;
                        break;
                    case 3: break;
                    case 4: c1 = c2 = c0; c3 = s[step]; break;
                    case 6: c1 = s[step]; c2 = s[step * 2]; c3 = s[step * 3]; break;
                    }
                    argb = type == 3 ? palette[c0] : c3 << 24 | c0 << 16 | c1 << 8 | c2;
                }
                uint32_t px = interlace ? ax[p] + x * dx[p] : x, py = interlace ? ay[p] + y * dy[p] : y;
                pixels[py * w + px] = argb;
            }
            last = cur;
            row += 1 + stride;
        }
    }
    vm_free(raw);
    *width = w; *height = h;
    return pixels;
}

N(image_decode)
{
    Array *in = (Array *)a[0].l;
    a[0].l = 0;
    if (!in) return;
    uint32_t w, h;
    uint32_t *pixels = png_decode(ADATA(in, uint8_t), (uint32_t)in->length, &w, &h);
    if (!pixels) return;
    Array *r = new_prim_array(t, T_INT, (jint)(w * h + 2));
    if (r) {
        ADATA(r, jint)[0] = (jint)w;
        ADATA(r, jint)[1] = (jint)h;
        memcpy(ADATA(r, jint) + 2, pixels, w * h * 4);
    }
    vm_free(pixels);
    a[0].l = r ? &r->o : 0;
}

static void be_put(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
N(image_encode)
{
    Array *px = (Array *)a[0].l;
    jint w = a[1].i, h = a[2].i, alpha = a[3].i;
    a[0].l = 0;
    if (!px || w <= 0 || h <= 0 || (int64_t)w * h > px->length) { throw_new(t, "java/lang/IllegalArgumentException", 0); return; }
    uint32_t channels = alpha ? 4 : 3, stride = 1 + (uint32_t)w * channels, raw_size = stride * (uint32_t)h;
    uint8_t *raw = vm_malloc(raw_size);
    uint32_t capacity = raw_size + raw_size / 4 + 128;
    uint8_t *z = vm_malloc(capacity);
    if (!raw || !z) { vm_free(raw); vm_free(z); throw_new(t, "java/lang/OutOfMemoryError", "png"); return; }
    for (jint y = 0; y < h; y++) {
        uint8_t *r = raw + y * stride;
        r[0] = 0;
        for (jint x = 0; x < w; x++) {
            uint32_t c = (uint32_t)ADATA(px, jint)[y * w + x];
            uint8_t *o = r + 1 + x * channels;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
            if (alpha) o[3] = (uint8_t)(c >> 24);
        }
    }
    /* zlib: header, raw deflate, Adler-32. */
    z[0] = 0x78; z[1] = 0x01;
    uint32_t zn = 2 + bjvm_deflate(raw, raw_size, z + 2, capacity - 6);
    uint32_t s1 = 1, s2 = 0;
    for (uint32_t i = 0; i < raw_size; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
    be_put(z + zn, s2 << 16 | s1); zn += 4;
    vm_free(raw);
    uint32_t total = 8 + 25 + 12 + zn + 12;
    Array *r = new_prim_array(t, T_BYTE, (jint)total);
    if (!r) { vm_free(z); return; }
    uint8_t *o = ADATA(r, uint8_t);
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    memcpy(o, sig, 8);
    uint8_t *c = o + 8;
    be_put(c, 13); memcpy(c + 4, "IHDR", 4); be_put(c + 8, (uint32_t)w); be_put(c + 12, (uint32_t)h);
    c[16] = 8; c[17] = alpha ? 6 : 2; c[18] = c[19] = c[20] = 0;
    be_put(c + 21, bjvm_crc32(0, c + 4, 17));
    c += 25;
    be_put(c, zn); memcpy(c + 4, "IDAT", 4); memcpy(c + 8, z, zn); be_put(c + 8 + zn, bjvm_crc32(0, c + 4, zn + 4));
    c += 12 + zn;
    be_put(c, 0); memcpy(c + 4, "IEND", 4); be_put(c + 8, bjvm_crc32(0, c + 4, 4));
    vm_free(z);
    a[0].l = &r->o;
}

/* ---- java.nio raw memory: (array, byte offset) addressing ---- */
static uint8_t *raw(Thread *t, Object *array, jint offset, jint size)
{
    Array *x = (Array *)array;
    if (!x) { throw_npe(t); return 0; }
    uint32_t bytes = (uint32_t)x->length * x->o.cls->elem_size;
    if (offset < 0 || size < 0 || (uint32_t)offset + (uint32_t)size > bytes) {
        throw_new(t, "java/lang/IndexOutOfBoundsException", 0);
        return 0;
    }
    return ADATA(x, uint8_t) + offset;
}
N(memory_get)
{
    jint size = a[2].i;
    uint8_t *p = raw(t, a[0].l, a[1].i, size);
    if (!p) return;
    uint32_t v = 0;
    memcpy(&v, p, (size_t)size);
    if (a[3].i) v = size == 2 ? (uint32_t)__builtin_bswap16((uint16_t)v) : size == 4 ? __builtin_bswap32(v) : v;
    a[0].i = size == 1 ? (int8_t)v : size == 2 ? (int16_t)v : (jint)v;
}
N(memory_put)
{
    jint size = a[2].i;
    uint8_t *p = raw(t, a[0].l, a[1].i, size);
    if (!p) return;
    uint32_t v = (uint32_t)a[3].i;
    if (a[4].i) v = size == 2 ? (uint32_t)__builtin_bswap16((uint16_t)v) : size == 4 ? __builtin_bswap32(v) : v;
    memcpy(p, &v, (size_t)size);
}
N(memory_get_long)
{
    uint8_t *p = raw(t, a[0].l, a[1].i, 8);
    if (!p) return;
    uint64_t v;
    memcpy(&v, p, 8);
    if (a[2].i) v = __builtin_bswap64(v);
    put_long(a, (jlong)v);
}
N(memory_put_long)
{
    uint8_t *p = raw(t, a[0].l, a[1].i, 8);
    if (!p) return;
    uint64_t v = (uint64_t)get_long(a + 2);
    if (a[4].i) v = __builtin_bswap64(v);
    memcpy(p, &v, 8);
}
N(memory_copy)
{
    jint bytes = a[4].i, size = a[5].i;
    uint8_t *s = raw(t, a[0].l, a[1].i, bytes), *d = s ? raw(t, a[2].l, a[3].i, bytes) : 0;
    if (!d) return;
    memmove(d, s, (size_t)bytes);
    if (a[6].i && size > 1)
        for (jint i = 0; i + size <= bytes; i += size)
            for (jint k = 0; k < size / 2; k++) { uint8_t c = d[i + k]; d[i + k] = d[i + size - 1 - k]; d[i + size - 1 - k] = c; }
}

/* ---- java.net through the platform TCP stack ---- */
N(inet_resolve)
{
    char host[256];
    if (!path_arg(t, a[0].l, host, sizeof(host))) return;
    uint32_t address = 0;
    a[0].i = plat_resolve(host, &address) ? 0 : (jint)address;
}
N(socket_open)
{
    char host[256];
    if (!path_arg(t, a[0].l, host, sizeof(host))) return;
    a[0].i = plat_net_open(host, (uint32_t)a[1].i);
}
N(socket_send)
{
    Array *b = (Array *)a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (!b || off < 0 || n < 0 || off > b->length - n) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    a[0].i = plat_net_send(a[0].i, ADATA(b, uint8_t) + off, (uint32_t)n);
}
N(socket_recv)
{
    Array *b = (Array *)a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (!b || off < 0 || n < 0 || off > b->length - n) { throw_new(t, "java/lang/IndexOutOfBoundsException", 0); return; }
    a[0].i = plat_net_recv(a[0].i, ADATA(b, uint8_t) + off, (uint32_t)n, (uint32_t)a[4].i);
}
N(socket_close) { (void)t; plat_net_close(a[0].i); }

const NativeEntry natives_io[] = {
    {"java/net/InetAddress", "resolve0", "(Ljava/lang/String;)I", inet_resolve},
    {"java/net/Socket", "open0", "(Ljava/lang/String;I)I", socket_open},
    {"java/net/Socket", "send0", "(I[BII)I", socket_send},
    {"java/net/Socket", "recv0", "(I[BIII)I", socket_recv},
    {"java/net/Socket", "close0", "(I)V", socket_close},
    {"java/io/File", "exists0", "(Ljava/lang/String;)Z", file_exists},
    {"java/io/File", "isDirectory0", "(Ljava/lang/String;)Z", file_is_dir},
    {"java/io/File", "length0", "(Ljava/lang/String;)J", file_length},
    {"java/io/File", "list0", "(Ljava/lang/String;)[Ljava/lang/String;", file_list},
    {"java/io/File", "mkdir0", "(Ljava/lang/String;)Z", file_mkdir},
    {"java/io/File", "delete0", "(Ljava/lang/String;)Z", file_delete},
    {"java/io/File", "rename0", "(Ljava/lang/String;Ljava/lang/String;)Z", file_rename},
    {"java/io/File", "cwd0", "()Ljava/lang/String;", file_cwd},
    {"java/io/FileInputStream", "read0", "(Ljava/lang/String;J[BII)I", fis_read},
    {"java/io/FileOutputStream", "write0", "(Ljava/lang/String;[BIIZ)Z", fos_write},
    {"java/util/zip/Inflater", "inflate0", "([BIIZ)[B", inflater_inflate},
    {"java/util/zip/Deflater", "deflate0", "([BII)[B", deflater_deflate},
    {"java/util/zip/CRC32", "update0", "(I[BII)I", crc_update},
    {"javax/imageio/ImageIO", "decode0", "([B)[I", image_decode},
    {"javax/imageio/ImageIO", "encode0", "([IIIZ)[B", image_encode},
    {"java/nio/Memory", "get", "(Ljava/lang/Object;IIZ)I", memory_get},
    {"java/nio/Memory", "put", "(Ljava/lang/Object;IIIZ)V", memory_put},
    {"java/nio/Memory", "getLong", "(Ljava/lang/Object;IZ)J", memory_get_long},
    {"java/nio/Memory", "putLong", "(Ljava/lang/Object;IJZ)V", memory_put_long},
    {"java/nio/Memory", "copy", "(Ljava/lang/Object;ILjava/lang/Object;IIIZ)V", memory_copy},
    {0, 0, 0, 0}
};
