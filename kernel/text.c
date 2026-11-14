#include "text.h"
int utf8_feed(Utf8Decoder *s, unsigned char b, unsigned int out[2])
{
    int n = 0;
    if (s->remaining) {
        if ((b & 0xc0) == 0x80) {
            s->value = (s->value << 6) | (b & 63);
            if (--s->remaining) return 0;
            unsigned int c = s->value;
            out[0] = c < s->minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ? '?' : c;
            return 1;
        }
        s->remaining = 0;
        out[n++] = '?';
    }
    if (b < 0x80) out[n++] = b;
    else if (b >= 0xc2 && b <= 0xf4) {
        s->remaining = b < 0xe0 ? 1 : b < 0xf0 ? 2 : 3;
        s->minimum = b < 0xe0 ? 0x80 : b < 0xf0 ? 0x800 : 0x10000;
        s->value = b & (b < 0xe0 ? 31 : b < 0xf0 ? 15 : 7);
    } else out[n++] = '?';
    return n;
}
unsigned int utf8_encode(unsigned int c, char out[4])
{
    if (c < 0x80) { out[0] = c; return 1; }
    if (c < 0x800) { out[0] = 0xc0 | (c >> 6); out[1] = 0x80 | (c & 63); return 2; }
    if (c >= 0xd800 && c <= 0xdfff) { out[0] = '?'; return 1; }
    if (c < 0x10000) {
        out[0] = 0xe0 | (c >> 12); out[1] = 0x80 | ((c >> 6) & 63); out[2] = 0x80 | (c & 63); return 3;
    }
    if (c > 0x10ffff) { out[0] = '?'; return 1; }
    out[0] = 0xf0 | (c >> 18); out[1] = 0x80 | ((c >> 12) & 63);
    out[2] = 0x80 | ((c >> 6) & 63); out[3] = 0x80 | (c & 63); return 4;
}
unsigned char text_glyph(unsigned int c)
{
    if (c < 128) return c;
    if (c >= 0x410 && c <= 0x43f) return 0x80 + c - 0x410;
    if (c >= 0x440 && c <= 0x44f) return 0xe0 + c - 0x440;
    if (c == 0x401) return 0xf0;
    if (c == 0x451) return 0xf1;
    if (c == 0x2116) return 0xf2;
    return '?';
}
