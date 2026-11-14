#include <barnix_gfx.h>
#include "text.h"
/* Barnix Graphics: canvas drawing, input state and timing over ABI 10. */
static unsigned char held[256];
static unsigned int last_buttons;
static int last_wheel, wheel_ready;

static int clip(const BgxCanvas *c, int *x, int *y, int *w, int *h)
{
    if (!c || !c->pixels) return 0;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*w > c->width - *x) *w = c->width - *x;
    if (*h > c->height - *y) *h = c->height - *y;
    return *w > 0 && *h > 0;
}
static unsigned int mix(unsigned int under, unsigned int over, int alpha)
{
    unsigned int a = (unsigned int)alpha, b = 256 - a;   /* alpha 0..256 */
    unsigned int rb = ((over & 0xff00ff) * a + (under & 0xff00ff) * b) >> 8 & 0xff00ff;
    unsigned int g = ((over & 0x00ff00) * a + (under & 0x00ff00) * b) >> 8 & 0x00ff00;
    return rb | g;
}

int bgx_open(BgxCanvas *canvas, unsigned int *pixels, int width, int height)
{
    if (!canvas || !pixels || width <= 0 || height <= 0 || barnix->version < 10) return -1;
    canvas->pixels = pixels; canvas->width = width; canvas->height = height;
    bgx_clear(canvas, BGX_BLACK);
    BgxEvent event;
    while (bgx_poll(&event)) {}
    int dx, dy; barnix->motion(&dx, &dy);
    BarnixPointer pointer; barnix->pointer(&pointer);
    last_buttons = pointer.mouse.buttons; last_wheel = pointer.wheel; wheel_ready = 1;
    return bgx_present(canvas);
}
int bgx_present(const BgxCanvas *c)
{
    if (!c || !c->pixels) return -1;
    return barnix->present(c->pixels, c->width, c->height);
}
void bgx_close(void) { barnix->present(0, 0, 0); }
int bgx_display(int *width, int *height)
{
    unsigned int w = 0, h = 0;
    int result = barnix->display(&w, &h);
    if (width) *width = w;
    if (height) *height = h;
    return result;
}

void bgx_clear(BgxCanvas *c, unsigned int color)
{
    if (!c || !c->pixels) return;
    for (int i = 0; i < c->width * c->height; i++) c->pixels[i] = color;
}
void bgx_fill(BgxCanvas *c, int x, int y, int w, int h, unsigned int color)
{
    if (!clip(c, &x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        unsigned int *p = c->pixels + (y + j) * c->width + x;
        for (int i = 0; i < w; i++) p[i] = color;
    }
}
void bgx_blend(BgxCanvas *c, int x, int y, int w, int h, unsigned int color, int alpha)
{
    if (alpha <= 0 || !clip(c, &x, &y, &w, &h)) return;
    if (alpha > 255) alpha = 256;
    for (int j = 0; j < h; j++) {
        unsigned int *p = c->pixels + (y + j) * c->width + x;
        for (int i = 0; i < w; i++) p[i] = mix(p[i], color, alpha);
    }
}
void bgx_frame(BgxCanvas *c, int x, int y, int w, int h, int t, unsigned int color)
{
    bgx_fill(c, x, y, w, t, color); bgx_fill(c, x, y + h - t, w, t, color);
    bgx_fill(c, x, y + t, t, h - 2 * t, color); bgx_fill(c, x + w - t, y + t, t, h - 2 * t, color);
}
void bgx_image(BgxCanvas *c, int x, int y, const unsigned int *src, int w, int h, int scale)
{
    if (!c || !c->pixels || !src || scale < 1) return;
    for (int j = 0; j < h * scale; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= c->height) continue;
        const unsigned int *line = src + j / scale * w;
        for (int i = 0; i < w * scale; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= c->width) continue;
            unsigned int v = line[i / scale], a = v >> 24;
            unsigned int *p = c->pixels + yy * c->width + xx;
            if (a >= 255) *p = v & 0xffffff;
            else if (a) *p = mix(*p, v, a + 1);
        }
    }
}
void bgx_upscale(BgxCanvas *c, int x, int y, const BgxCanvas *src, int scale)
{
    if (!c || !c->pixels || !src || !src->pixels || scale < 1) return;
    for (int j = 0; j < src->height * scale; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= c->height) continue;
        const unsigned int *line = src->pixels + j / scale * src->width;
        unsigned int *out = c->pixels + yy * c->width;
        if (scale == 2 && x >= 0 && x + src->width * 2 <= c->width) {   /* common fast path */
            out += x;
            for (int i = 0; i < src->width; i++) out[2 * i] = out[2 * i + 1] = line[i];
            continue;
        }
        for (int i = 0; i < src->width * scale; i++)
            if (x + i >= 0 && x + i < c->width) out[x + i] = line[i / scale];
    }
}

/* Glyph rows come from the kernel once, then from this cache. */
static unsigned char glyph_rows[256][16], glyph_ready[256];
static const unsigned char *glyph(unsigned int code)
{
    code &= 255;
    if (!glyph_ready[code]) {
        for (int row = 0; row < 16; row++) glyph_rows[code][row] = barnix->glyph(code, row);
        glyph_ready[code] = 1;
    }
    return glyph_rows[code];
}
int bgx_text(BgxCanvas *c, int x, int y, const char *text, unsigned int color, int scale)
{
    Utf8Decoder decoder = {0, 0, 0};
    int start = x;
    if (!text || scale < 1) return 0;
    while (*text) {
        unsigned int out[2];
        int count = utf8_feed(&decoder, (unsigned char)*text++, out);
        for (int k = 0; k < count; k++, x += 8 * scale) {
            if (!c || !c->pixels || x >= c->width || y >= c->height || x + 8 * scale <= 0 || y + 16 * scale <= 0) continue;
            const unsigned char *rows = glyph(text_glyph(out[k]));
            for (int row = 0; row < 16; row++) {
                unsigned int bits = rows[row];
                if (!bits) continue;
                if (scale == 1) {
                    int yy = y + row;
                    if (yy < 0 || yy >= c->height) continue;
                    unsigned int *line = c->pixels + yy * c->width;
                    for (int b = 0; b < 8; b++)
                        if (bits & (128U >> b) && x + b >= 0 && x + b < c->width) line[x + b] = color;
                } else
                    for (int b = 0; b < 8; b++)
                        if (bits & (128U >> b)) bgx_fill(c, x + b * scale, y + row * scale, scale, scale, color);
            }
        }
    }
    return x - start;
}
int bgx_text_shadow(BgxCanvas *c, int x, int y, const char *text, unsigned int color, int scale)
{
    bgx_text(c, x + scale, y + scale, text, 0x000000, scale);
    return bgx_text(c, x, y, text, color, scale);
}
int bgx_text_width(const char *text, int scale) { return bgx_text(0, 0, 0, text, 0, scale); }
void bgx_cursor(BgxCanvas *c, int x, int y)
{
    for (int j = 0; j < 14; j++)
        for (int i = 0; i <= j / 2 + 1 && i < 9; i++) {
            int edge = i == 0 || i == j / 2 + 1 || j == 13;
            bgx_fill(c, x + i, y + j, 1, 1, edge ? BGX_BLACK : BGX_WHITE);
        }
}

int bgx_poll(BgxEvent *event)
{
    BarnixKeyEvent raw;
    if (!barnix->key_event(&raw)) return 0;
    held[raw.code & 255] = raw.pressed;
    if (event) {
        event->type = raw.pressed ? BGX_PRESS : BGX_RELEASE;
        event->code = raw.code; event->key = raw.key; event->repeat = raw.repeat;
    }
    return 1;
}
int bgx_key_held(unsigned int code) { return code < 256 && held[code]; }
/* After a nested program: its key releases (Ctrl of Ctrl+F5) went to it. */
void bgx_keys_release(void) { for (int i = 0; i < 256; i++) held[i] = 0; }
void bgx_mouse(BgxMouse *m)
{
    BarnixPointer pointer;
    barnix->pointer(&pointer);
    if (!wheel_ready) { last_wheel = pointer.wheel; last_buttons = pointer.mouse.buttons; wheel_ready = 1; }
    if (!m) return;
    barnix->motion(&m->dx, &m->dy);
    m->x = pointer.mouse.x; m->y = pointer.mouse.y; m->available = pointer.mouse.available;
    m->buttons = pointer.mouse.buttons;
    m->pressed = m->buttons & ~last_buttons; m->released = last_buttons & ~m->buttons;
    m->wheel = pointer.wheel - last_wheel;
    last_buttons = m->buttons; last_wheel = pointer.wheel;
}

static unsigned long long tsc(void)
{
    unsigned int lo, hi;
    __asm__ volatile(".byte 0x0f,0x31" : "=a"(lo), "=d"(hi));
    return (unsigned long long)hi << 32 | lo;
}
unsigned int bgx_millis(void)
{
    static unsigned long long last;
    static unsigned int per_ms, total;
    if (!per_ms) {   /* calibrate against the PIT-timed sleep */
        unsigned long long start = tsc();
        barnix->sleep_ms(50);
        unsigned long long span = tsc() - start;
        per_ms = span >> 32 ? 0xffffffffU : (unsigned int)span / 50;
        if (!per_ms) per_ms = 1;
        last = tsc();
    }
    unsigned long long delta = tsc() - last;
    unsigned int ms = delta >> 32 ? 0xffffffffU / per_ms : (unsigned int)delta / per_ms;
    last += (unsigned long long)ms * per_ms;
    return total += ms;
}

unsigned int bgx_cpus(void) { return barnix->version >= 11 ? barnix->cpus() : 1; }
void bgx_parallel(void (*job)(void *, unsigned int), void *argument, unsigned int count)
{
    if (barnix->version >= 11) { barnix->parallel(job, argument, count); return; }
    for (unsigned int i = 0; i < count; i++) job(argument, i);
}
