/* Gouse Web: fonts. scripts/web_font.py draws Liberation Sans, Serif and
 * Mono once at a base size; here glyphs are scaled to any CSS size with an
 * area filter (exact for shrinking, smooth enough for growing), cached,
 * and drawn with alpha. Bold and italic of faces without them are made by
 * widening and slanting. Without the font file the 8x16 system font
 * stands in for every face. */
#include "web.h"

typedef struct {
    int base, ascent, descent, count;
    const unsigned char *glyphs;    /* 16 bytes each */
    const unsigned char *bitmaps;
} Face;
static Face faces[WF_FACES];
static int ready;
static unsigned char *cache_base;
static unsigned int cache_size, cache_used;
typedef struct Cached {
    unsigned int code;
    short size, w, h, left, top;
    unsigned char face, bold;
    struct Cached *next;
    unsigned char alpha[];
} Cached;
#define CACHE_BUCKETS 1024
static Cached *buckets[CACHE_BUCKETS];

static unsigned int get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }
static unsigned int get16(const unsigned char *p) { return p[0] | p[1] << 8; }

int web_font_ready(void) { return ready; }
int web_font_init(const void *data, unsigned int length, void *memory, unsigned int bytes)
{
    ready = 0;
    if (!memory || bytes < (1u << 20)) return -1;
    unsigned char *out = memory;
    unsigned int font_room = bytes / 3;
    long n = -1;
    const unsigned char *raw = data;
    if (raw && length >= 4 && raw[0] == 'B' && raw[1] == 'W' && raw[2] == 'F' && raw[3] == '1' && length <= font_room) {
        for (unsigned int i = 0; i < length; i++) out[i] = raw[i];   /* already inflated (the system font) */
        n = length;
    } else if (raw) n = web_inflate(data, length, out, font_room, 1);
    if (n < 6 || out[0] != 'B' || out[1] != 'W' || out[2] != 'F' || out[3] != '1') return -1;
    int count = (int)get16(out + 4);
    const unsigned char *p = out + 6, *end = out + n;
    for (int f = 0; f < count && f < WF_FACES; f++) {
        if (p + 26 > end) return -1;
        Face *face = &faces[f];
        face->base = (int)get16(p + 12);
        face->ascent = (short)get16(p + 14);
        face->descent = (short)get16(p + 16);
        face->count = (int)get16(p + 18);
        unsigned int bitmap_bytes = get32(p + 20);
        face->glyphs = p + 24;
        face->bitmaps = face->glyphs + face->count * 16;
        p = face->bitmaps + bitmap_bytes;
        if (p > end || face->base < 8) return -1;
    }
    for (int f = count; f < WF_FACES; f++) faces[f] = faces[0];
    cache_base = out + ((n + 15) & ~15L);
    cache_size = bytes - (unsigned int)((n + 15) & ~15L);
    cache_used = 0;
    for (int i = 0; i < CACHE_BUCKETS; i++) buckets[i] = 0;
    ready = 1;
    return 0;
}

static const unsigned char *find(const Face *f, unsigned int code)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        unsigned int c = get32(f->glyphs + mid * 16);
        if (c == code) return f->glyphs + mid * 16;
        if (c < code) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
static int face_index(const WFont *f) { return f->family == 1 ? WF_SERIF : f->family == 2 ? WF_MONO : f->bold ? WF_SANS_BOLD : WF_SANS; }
static int synthetic_bold(const WFont *f) { return f->bold && f->family != 0; }
static int invisible(unsigned int code)
{
    return code == 0xad || (code >= 0x200b && code <= 0x200f) || code == 0xfeff || (code >= 0x2028 && code <= 0x202e) || (code >= 0x2060 && code <= 0x206f) ||
           (code >= 0x300 && code <= 0x36f) || (code >= 0x483 && code <= 0x489) || (code >= 0xfe00 && code <= 0xfe0f);   /* combining marks: not drawn */
}
/* The glyph of code in the font's face, falling back to plain sans. */
static const unsigned char *glyph_of(const WFont *font, unsigned int code, const Face **face, int *index)
{
    int fi = face_index(font);
    if (code == 0xa0 || code == '\t' || code == 0x2002 || code == 0x2003 || code == 0x2009) code = ' ';
    const unsigned char *g = find(&faces[fi], code);
    if (!g && fi != WF_SANS) { fi = font->bold ? WF_SANS_BOLD : WF_SANS; g = find(&faces[fi], code); }
    if (!g) { fi = WF_SANS; g = find(&faces[fi], 0xfffd); if (!g) g = find(&faces[fi], '?'); }
    *face = &faces[fi]; *index = fi;
    return g;
}
int wf_advance(const WFont *f, unsigned int code)
{
    if (!ready || invisible(code)) return 0;
    const Face *face; int fi;
    const unsigned char *g = glyph_of(f, code, &face, &fi);
    if (!g) return f->size * 8;
    int advance = (int)get16(g + 4) * f->size / face->base;
    if (synthetic_bold(f)) advance += f->size >= 20 ? 32 : 16;
    return advance;
}
int wf_ascent(const WFont *f) { const Face *face = &faces[face_index(f)]; return ready ? (face->ascent * f->size + face->base / 2) / face->base : f->size * 3 / 4; }
int wf_descent(const WFont *f) { const Face *face = &faces[face_index(f)]; return ready ? (face->descent * f->size + face->base / 2) / face->base : f->size / 4; }
int wf_text_width(const WFont *f, const char *text, int length, int letter_spacing)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + length;
    int width = 0;
    while (p < end) {
        unsigned int c = (unsigned int)w_utf8_decode(&p, end);
        width += wf_advance(f, c) + letter_spacing * 16;
    }
    return width;
}

/* ---------------------------------------------------------------- scaling */
/* dst[i] = average of src over [i*from/to, (i+1)*from/to) (area filter). */
static void resample(const int *src, int src_count, int src_stride, int *dst, int dst_count, int dst_stride, int from, int to)
{
    for (int i = 0; i < dst_count; i++) {
        int a = i * from * 256 / to, b = (i + 1) * from * 256 / to;
        if (b <= a) b = a + 1;
        int acc = 0;
        for (int j = a >> 8; j <= (b - 1) >> 8; j++) {
            if (j >= src_count) break;
            int lo = a > j * 256 ? a : j * 256, hi = b < (j + 1) * 256 ? b : (j + 1) * 256;
            acc += src[j * src_stride] * (hi - lo);
        }
        dst[i * dst_stride] = acc / (b - a);
    }
}
static int horizontal[160 * 160], vertical[160 * 160], source[160 * 160];
static Cached *render(const WFont *font, unsigned int code)
{
    const Face *face; int fi;
    const unsigned char *g = glyph_of(font, code, &face, &fi);
    if (!g) return 0;
    int size = font->size > 150 ? 150 : font->size, bold = synthetic_bold(font);
    unsigned int key = (code * 31u + (unsigned)size * 7u + (unsigned)fi * 3u + (unsigned)bold) & (CACHE_BUCKETS - 1);
    for (Cached *c = buckets[key]; c; c = c->next)
        if (c->code == code && c->size == size && c->face == fi && c->bold == bold) return c;
    int w = g[8], h = g[9], left = (signed char)g[6], top = (signed char)g[7];
    const unsigned char *bits = face->bitmaps + get32(g + 12);
    int base = face->base;
    int tw = (w * size + base - 1) / base + 1, th = (h * size + base - 1) / base + 1;
    if (w > 150 || h > 150 || tw > 150 || th > 150) return 0;
    int extra = bold ? (size >= 20 ? 2 : 1) : 0;
    unsigned int bytes = (unsigned)(sizeof(Cached) + (unsigned)((tw + extra) * th) + 7) & ~7u;
    if (cache_used + bytes > cache_size) {   /* full: start again */
        cache_used = 0;
        for (int i = 0; i < CACHE_BUCKETS; i++) buckets[i] = 0;
    }
    Cached *c = (Cached *)(cache_base + cache_used);
    cache_used += bytes;
    c->code = code; c->size = (short)size; c->face = (unsigned char)fi; c->bold = (unsigned char)bold;
    c->w = (short)(tw + extra); c->h = (short)th;
    c->left = (short)(left >= 0 ? left * size / base : -((-left * size + base - 1) / base));
    c->top = (short)((top * size + base / 2) / base);
    int row_bytes = (w + 1) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned int b = bits[y * row_bytes + x / 2];
            source[y * w + x] = (int)((x & 1 ? b & 15 : b >> 4) * 17);
        }
    for (int y = 0; y < h; y++) resample(source + y * w, w, 1, horizontal + y * tw, tw, 1, base, size);
    for (int x = 0; x < tw; x++) resample(horizontal + x, h, tw, vertical + x, th, tw, base, size);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw + extra; x++) {
            int a = 0;
            for (int k = 0; k <= extra; k++) { int sx = x - k; if (sx >= 0 && sx < tw && vertical[y * tw + sx] > a) a = vertical[y * tw + sx]; }
            if (a > 255) a = 255;
            a = a * (255 + (255 - a) / 2) / 255;   /* a little darker: small text stays legible */
            c->alpha[y * (tw + extra) + x] = (unsigned char)(a > 255 ? 255 : a);
        }
    c->next = buckets[key];
    buckets[key] = c;
    return c;
}

static inline unsigned int blend(unsigned int dst, unsigned int color, int a)
{
    if (a >= 255) return color & 0xffffff;
    unsigned int k = (unsigned)a + (a >> 7), j = 256 - k;
    unsigned int rb = ((color & 0xff00ff) * k + (dst & 0xff00ff) * j) >> 8 & 0xff00ff;
    unsigned int g = ((color & 0x00ff00) * k + (dst & 0x00ff00) * j) >> 8 & 0x00ff00;
    return rb | g;
}
void wf_draw(WCanvas *cv, int x, int y, const WFont *f, const char *text, int length, unsigned int argb,
             int letter_spacing, int cx0, int cy0, int cx1, int cy1)
{
    if (!ready) return;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > cv->width) cx1 = cv->width;
    if (cy1 > cv->height) cy1 = cv->height;
    int alpha = (int)(argb >> 24);
    if (!alpha) return;
    const unsigned char *p = (const unsigned char *)text, *end = p + length;
    int pen = x * 16;
    while (p < end) {
        unsigned int code = (unsigned int)w_utf8_decode(&p, end);
        int advance = wf_advance(f, code);
        if (code > ' ' && code != 0xa0 && !invisible(code)) {
            Cached *g = render(f, code);
            if (g) {
                int gx = (pen + 8) / 16 + g->left, gy = y - g->top;
                for (int r = 0; r < g->h; r++) {
                    int yy = gy + r;
                    if (yy < cy0 || yy >= cy1) continue;
                    int slant = f->italic ? ((g->top - r) * 13) >> 6 : 0;
                    unsigned int *line = cv->pixels + yy * cv->width;
                    const unsigned char *row = g->alpha + r * g->w;
                    for (int k = 0; k < g->w; k++) {
                        int xx = gx + k + slant;
                        if (xx < cx0 || xx >= cx1 || !row[k]) continue;
                        line[xx] = blend(line[xx], argb, row[k] * alpha / 255);
                    }
                }
            }
        }
        pen += advance + letter_spacing * 16;
    }
}
