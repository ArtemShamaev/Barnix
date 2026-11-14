/* Happy Gouse helpers: strings, formatting, time, files, drawing and widgets. */
#include "gouse.h"
#include <barnix_net.h>
#include "../../kernel/text.h"

/* ---------------------------------------------------------------- strings */
int g_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
void g_copy(char *out, const char *s, int capacity)
{
    int i = 0;
    if (capacity <= 0) return;
    while (s && s[i] && i + 1 < capacity) { out[i] = s[i]; i++; }
    /* never leave half of a UTF-8 sequence */
    if (s && s[i]) while (i > 0 && ((unsigned char)out[i] & 0xc0) == 0x80 && ((unsigned char)s[i] & 0xc0) == 0x80) i--;
    out[i] = 0;
}
void g_append(char *out, const char *s, int capacity) { int n = g_len(out); if (n < capacity) g_copy(out + n, s, capacity - n); }
int g_equal(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int g_equal_ci(const char *a, const char *b) { while (*a && lower(*a) == lower(*b)) { a++; b++; } return lower(*a) == lower(*b); }
int g_ends_ci(const char *s, const char *suffix)
{
    int a = g_len(s), b = g_len(suffix);
    return a >= b && g_equal_ci(s + a - b, suffix);
}
int g_atoi(const char *s)
{
    int sign = 1, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    return v * sign;
}

/* %d %u %x %s %c with optional zero padding and width, e.g. %02d. */
int g_vformat(char *out, int capacity, const char *f, va_list args)
{
    f = g_tr(f);   /* formats are translated like other text */
    int n = 0;
#define PUT(ch) do { if (n + 1 < capacity) out[n] = (ch); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int zero = 0, width = 0;
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        char digits[16]; int k = 0;
        if (*f == 's') {
            const char *s = va_arg(args, const char *);
            int len = g_len(s);
            for (int i = len; i < width; i++) PUT(' ');
            while (s && *s) PUT(*s++);
            continue;
        }
        if (*f == 'c') { PUT((char)va_arg(args, int)); continue; }
        if (*f == '%') { PUT('%'); continue; }
        unsigned int v; int negative = 0;
        if (*f == 'd') { int x = va_arg(args, int); negative = x < 0; v = negative ? 0U - (unsigned int)x : (unsigned int)x; }
        else v = va_arg(args, unsigned int);
        unsigned int base = *f == 'x' ? 16 : 10;
        do { digits[k++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
        if (negative) width--;
        if (negative && zero) PUT('-');
        for (int i = k; i < width; i++) PUT(zero ? '0' : ' ');
        if (negative && !zero) PUT('-');
        while (k) PUT(digits[--k]);
    }
    if (capacity > 0) out[n < capacity ? n : capacity - 1] = 0;
#undef PUT
    return n;
}
int g_format(char *out, int capacity, const char *format, ...)
{
    va_list args; va_start(args, format);
    int n = g_vformat(out, capacity, format, args);
    va_end(args);
    return n;
}
/* There is no libgcc: 64-bit division by shift and subtract. */
unsigned long long g_udiv64(unsigned long long n, unsigned long long d, unsigned long long *remainder)
{
    unsigned long long q = 0, r = 0;
    if (!d) { if (remainder) *remainder = 0; return 0; }
    for (int i = 63; i >= 0; i--) {
        r = r << 1 | (n >> i & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (remainder) *remainder = r;
    return q;
}
unsigned int g_random(void)
{
    static unsigned int seed;
    if (!seed) { unsigned int lo, hi; __asm__ volatile(".byte 0x0f,0x31" : "=a"(lo), "=d"(hi)); seed = lo ^ hi ^ 0x9e3779b9U; if (!seed) seed = 1; }
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
int g_isqrt(int v)
{
    if (v <= 0) return 0;
    unsigned int r = 0, bit = 1U << 30, n = (unsigned int)v;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}
int g_sin60(int index) { return gouse_sin60[((index % 60) + 60) % 60]; }
int g_utf8_prev(const char *s, int pos)
{
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xc0) == 0x80) pos--;
    return pos;
}
int g_utf8_next(const char *s, int length, int pos)
{
    if (pos >= length) return length;
    pos++;
    while (pos < length && ((unsigned char)s[pos] & 0xc0) == 0x80) pos++;
    return pos;
}
int g_utf8_put(char *out, unsigned int c) { return (int)utf8_encode(c, out); }

/* ---------------------------------------------------------------- time */
const char *const g_month_names[12] = {"January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December"};
const char *const g_day_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static unsigned char cmos(unsigned char reg)
{
    unsigned char v;
    __asm__ volatile("outb %0,%1" :: "a"(reg), "Nd"((unsigned short)0x70));
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"((unsigned short)0x71));
    return v;
}
static int bcd(int v, int binary) { return binary ? v : (v >> 4) * 10 + (v & 15); }
/* The CMOS real-time clock; read twice so an update in between is caught. */
void g_time(GTime *t)
{
    unsigned char a[6], b[6];
    for (int tries = 0; tries < 5; tries++) {
        for (int i = 0; i < 1000 && (cmos(0x0a) & 0x80); i++) {}
        a[0] = cmos(0); a[1] = cmos(2); a[2] = cmos(4); a[3] = cmos(7); a[4] = cmos(8); a[5] = cmos(9);
        for (int i = 0; i < 1000 && (cmos(0x0a) & 0x80); i++) {}
        b[0] = cmos(0); b[1] = cmos(2); b[2] = cmos(4); b[3] = cmos(7); b[4] = cmos(8); b[5] = cmos(9);
        int same = 1;
        for (int i = 0; i < 6; i++) if (a[i] != b[i]) same = 0;
        if (same) break;
    }
    unsigned char status = cmos(0x0b);
    int binary = status & 4, hour = b[2] & 0x7f;
    t->second = bcd(b[0], binary); t->minute = bcd(b[1], binary);
    t->hour = bcd(hour, binary);
    if (!(status & 2) && (b[2] & 0x80)) t->hour = (t->hour % 12) + 12;   /* 12-hour clock, PM */
    else if (!(status & 2) && t->hour == 12) t->hour = 0;
    t->day = bcd(b[3], binary); t->month = bcd(b[4], binary); t->year = 2000 + bcd(b[5], binary);
    if (t->month < 1 || t->month > 12) t->month = 1;
    if (t->day < 1 || t->day > 31) t->day = 1;
    t->weekday = g_weekday(t->year, t->month, t->day);
}
int g_days_in_month(int year, int month)
{
    static const unsigned char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100) || year % 400 == 0)) return 29;
    return days[(month - 1) % 12];
}
int g_weekday(int year, int month, int day)
{
    static const int offset[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + offset[month - 1] + day) % 7;
}

/* ---------------------------------------------------------------- files */
const char *g_home(void)
{
    static char home[128];
    BarnixDirEntry entry; unsigned int position = 0;
    g_copy(home, "/home/", sizeof(home));
    g_append(home, barnix->current_user(), sizeof(home));
    if (barnix->directory(home, &position, &entry) < 0) g_copy(home, "/", sizeof(home));
    return home;
}
void g_path_join(char *out, int capacity, const char *dir, const char *name)
{
    if (name[0] == '/') { g_copy(out, name, capacity); return; }
    g_copy(out, dir, capacity);
    if (g_len(out) && out[g_len(out) - 1] != '/') g_append(out, "/", capacity);
    g_append(out, name, capacity);
}
int g_read_file(const char *path, char *buffer, int capacity)
{
    int size = barnix->size(path);
    if (size < 0) return -1;
    if (size > capacity) size = capacity;
    return barnix->read(path, 0, buffer, size) == size ? size : -1;
}
int g_write_file(const char *path, const char *data, int size) { return barnix->write(path, data, size); }

/* ---------------------------------------------------------------- drawing */
void g_bevel(BgxCanvas *c, int x, int y, int w, int h, int sunken)
{
    unsigned int a = sunken ? g_theme.shadow : g_theme.light, b = sunken ? g_theme.light : g_theme.dark;
    bgx_fill(c, x, y, w, 1, a); bgx_fill(c, x, y, 1, h, a);
    bgx_fill(c, x, y + h - 1, w, 1, b); bgx_fill(c, x + w - 1, y, 1, h, b);
    if (!sunken) { bgx_fill(c, x + 1, y + h - 2, w - 2, 1, g_theme.shadow); bgx_fill(c, x + w - 2, y + 1, 1, h - 2, g_theme.shadow); }
    else { bgx_fill(c, x + 1, y + 1, w - 2, 1, g_theme.dark); bgx_fill(c, x + 1, y + 1, 1, h - 2, g_theme.dark); }
}
void g_panel(BgxCanvas *c, int x, int y, int w, int h, unsigned int color, int sunken)
{
    bgx_fill(c, x, y, w, h, color);
    g_bevel(c, x, y, w, h, sunken);
}
void g_line(BgxCanvas *c, int x0, int y0, int x1, int y1, unsigned int color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 8192; guard++) {
        if ((unsigned)x0 < (unsigned)c->width && (unsigned)y0 < (unsigned)c->height) c->pixels[y0 * c->width + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void g_thick_line(BgxCanvas *c, int x0, int y0, int x1, int y1, int t, unsigned int color)
{
    if (t <= 1) { g_line(c, x0, y0, x1, y1, color); return; }
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int steps = dx > dy ? dx : dy;
    if (!steps) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
        g_disc(c, x, y, t / 2, color);
    }
}
void g_disc(BgxCanvas *c, int cx, int cy, int r, unsigned int color)
{
    for (int y = -r; y <= r; y++) {
        int half = g_isqrt(r * r - y * y + r);
        bgx_fill(c, cx - half, cy + y, 2 * half + 1, 1, color);
    }
}
void g_ring(BgxCanvas *c, int cx, int cy, int r, unsigned int color)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
        for (int i = 0; i < 8; i++) bgx_fill(c, cx + pts[i][0], cy + pts[i][1], 1, 1, color);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}
/* Ellipse inside the box: filled rows, or the outline from neighbouring rows. */
void g_ellipse(BgxCanvas *c, int x0, int y0, int x1, int y1, unsigned int color, int filled)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int a2 = x1 - x0, b2 = y1 - y0;   /* diameters */
    if (!a2 || !b2) { g_line(c, x0, y0, x1, y1, color); return; }
    int prev_l = -1, prev_r = -1;
    for (int y = y0; y <= y1; y++) {
        /* (2x-x0-x1)^2/a2^2 + (2y-y0-y1)^2/b2^2 <= 1 */
        int dy = 2 * y - y0 - y1, rest = b2 * b2 - dy * dy;
        int half = g_isqrt(rest < 0 ? 0 : rest) * a2 / b2 / 2;
        int l = (x0 + x1) / 2 - half, r = (x0 + x1 + 1) / 2 + half;
        if (filled) bgx_fill(c, l, y, r - l + 1, 1, color);
        else {
            int el = prev_l < 0 ? r : prev_l, er = prev_r < 0 ? l : prev_r;
            int a = l < el ? l : el, b = l < el ? el : l;
            bgx_fill(c, a, y, (b > a ? b - a : 0) + 1, 1, color);
            a = r < er ? r : er; b = r < er ? er : r;
            bgx_fill(c, a, y, (b > a ? b - a : 0) + 1, 1, color);
            if (y == y0 || y == y1) bgx_fill(c, l, y, r - l + 1, 1, color);
        }
        prev_l = l; prev_r = r;
    }
}
/* ---------------------------------------------------------------- language */
int g_russian;
typedef struct { const char *en, *ru; } GTranslation;
static const GTranslation translations[] = {
#include "gouse_ru.h"
};
#define TR_SLOTS 2048
static short tr_slot[TR_SLOTS];   /* index + 1 into translations, 0 empty */
static unsigned int tr_hash(const char *s) { unsigned int h = 2166136261U; while (*s) h = (h ^ (unsigned char)*s++) * 16777619U; return h; }
const char *g_tr(const char *english)
{
    static int ready;
    if (!g_russian || !english || !english[0]) return english;
    if (!ready) {
        for (int i = 0; i < (int)(sizeof(translations) / sizeof(translations[0])); i++) {
            unsigned int k = tr_hash(translations[i].en) & (TR_SLOTS - 1);
            while (tr_slot[k]) k = (k + 1) & (TR_SLOTS - 1);
            tr_slot[k] = (short)(i + 1);
        }
        ready = 1;
    }
    for (unsigned int k = tr_hash(english) & (TR_SLOTS - 1); tr_slot[k]; k = (k + 1) & (TR_SLOTS - 1))
        if (g_equal(translations[tr_slot[k] - 1].en, english)) return translations[tr_slot[k] - 1].ru;
    return english;
}
/* The bgx_text family through the translation (gouse.h redirects them here;
 * the parentheses call the real BDK functions). */
int g_tr_text(BgxCanvas *c, int x, int y, const char *s, unsigned int color, int scale) { return (bgx_text)(c, x, y, g_tr(s), color, scale); }
int g_tr_shadow(BgxCanvas *c, int x, int y, const char *s, unsigned int color, int scale) { return (bgx_text_shadow)(c, x, y, g_tr(s), color, scale); }
int g_tr_width(const char *s, int scale) { return (bgx_text_width)(g_tr(s), scale); }
/* Nominative and genitive month names, short day names. */
static const char *const months_ru[12] = {"Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"};
static const char *const months_of_ru[12] = {"января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"};
static const char *const days_ru[7] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
const char *g_month(int month) { month = (month - 1 + 120) % 12; return g_russian ? months_ru[month] : g_month_names[month]; }
const char *g_month_of(int month) { month = (month - 1 + 120) % 12; return g_russian ? months_of_ru[month] : g_month_names[month]; }
const char *g_day(int weekday) { weekday = (weekday + 70) % 7; return g_russian ? days_ru[weekday] : g_day_names[weekday]; }

int g_text(BgxCanvas *c, int x, int y, const char *s, unsigned int color) { return (bgx_text)(c, x, y, g_tr(s), color, 1); }
int g_text_raw(BgxCanvas *c, int x, int y, const char *s, unsigned int color) { return (bgx_text)(c, x, y, s, color, 1); }
int g_text_fit(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color) { return g_text_fit_raw(c, x, y, width, g_tr(s), color); }
/* Text cut to a width, ending with ".." when it does not fit. */
int g_text_fit_raw(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color)
{
    char buffer[256];
    int columns = width / 8, count = 0, i = 0;
    if (columns <= 0) return 0;
    while (s[i] && i < 250) {
        if (((unsigned char)s[i] & 0xc0) != 0x80) count++;
        if (count > columns) break;
        buffer[i] = s[i]; i++;
    }
    buffer[i] = 0;
    if (s[i] && columns >= 3) {
        int keep = 0, seen = 0;
        for (int k = 0; k < i; k++) { if (((unsigned char)buffer[k] & 0xc0) != 0x80) { if (seen == columns - 2) break; seen++; } keep = k + 1; }
        buffer[keep] = '.'; buffer[keep + 1] = '.'; buffer[keep + 2] = 0;
    }
    return g_text_raw(c, x, y, buffer, color);
}
void g_text_center(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color)
{
    s = g_tr(s);
    int w = (bgx_text_width)(s, 1);
    if (w > width) g_text_fit_raw(c, x, y, width, s, color);
    else g_text_raw(c, x + (width - w) / 2, y, s, color);
}
/* The logo and icons are deflated in gouse_assets.h; the first drawing
 * unpacks them. */
static const unsigned int *assets(void)
{
    static unsigned int *pixels;
    static int failed;
    if (!pixels && !failed) {
        pixels = g_alloc(GOUSE_PIXELS * 4);
        unsigned long used;
        if (!pixels || bnet_inflate(gouse_packed, sizeof(gouse_packed), &used, pixels, GOUSE_PIXELS * 4, 1) != GOUSE_PIXELS * 4) { failed = 1; pixels = 0; }
    }
    return pixels;
}
void g_icon(BgxCanvas *c, int x, int y, int icon, int size)
{
    const unsigned int *p = assets();
    if (icon < 0 || icon >= ICON_COUNT || !p) return;
    if (size >= 32) bgx_image(c, x, y, p + GOUSE_ICONS32 + icon * 1024, 32, 32, size / 32);
    else bgx_image(c, x, y, p + GOUSE_ICONS16 + icon * 256, 16, 16, 1);
}
void g_logo(BgxCanvas *c, int x, int y, int size)
{
    const unsigned int *p = assets();
    if (!p) return;
    if (size >= 128) bgx_image(c, x, y, p + GOUSE_LOGO128, 128, 128, size / 128);
    else if (size >= 48) bgx_image(c, x, y, p + GOUSE_LOGO48, 48, 48, 1);
    else bgx_image(c, x, y, p + GOUSE_LOGO16, 16, 16, 1);
}
int g_icon_named(const char *name)
{
    for (int i = 0; name && i < ICON_COUNT; i++) if (g_equal_ci(gouse_icon_names[i], name)) return i;
    return -1;
}

/* ---------------------------------------------------------------- widgets */
void g_zone(GWindow *w, int id, int x, int y, int width, int height)
{
    if (!w || w->zone_count >= MAX_ZONES) return;
    GZone *z = &w->zones[w->zone_count++];
    z->id = id; z->x = x; z->y = y; z->w = width; z->h = height;
}
void g_button(GWindow *w, BgxCanvas *c, int id, int x, int y, int width, int height, const char *label)
{
    int down = w && w->pressed_zone == id;
    g_panel(c, x, y, width, height, g_theme.face, down);
    g_text_center(c, x + 2 + down, y + (height - 16) / 2 + down, width - 4, label, g_theme.text);
    g_zone(w, id, x, y, width, height);
}
void g_toggle(GWindow *w, BgxCanvas *c, int id, int x, int y, int width, int height, const char *label, int on)
{
    g_panel(c, x, y, width, height, on ? g_theme.highlight : g_theme.face, on || (w && w->pressed_zone == id));
    g_text_center(c, x + 2, y + (height - 16) / 2, width - 4, label, on ? g_theme.highlight_text : g_theme.text);
    g_zone(w, id, x, y, width, height);
}
void g_check(GWindow *w, BgxCanvas *c, int id, int x, int y, const char *label, int checked)
{
    g_panel(c, x, y + 2, 13, 13, g_theme.window, 1);
    if (checked) {
        g_line(c, x + 3, y + 8, x + 5, y + 11, g_theme.text); g_line(c, x + 3, y + 9, x + 5, y + 12, g_theme.text);
        g_line(c, x + 5, y + 11, x + 10, y + 5, g_theme.text); g_line(c, x + 5, y + 12, x + 10, y + 6, g_theme.text);
    }
    int width = g_text(c, x + 18, y, label, g_theme.text);
    g_zone(w, id, x, y, 18 + width, 17);
}
void g_scrollbar(BgxCanvas *c, int x, int y, int h, int total, int first, int visible)
{
    g_panel(c, x, y, 14, h, g_theme.face, 1);
    if (total <= visible || h < 20) return;
    int thumb = h * visible / total;
    if (thumb < 16) thumb = 16;
    int pos = (h - thumb) * first / (total - visible);
    g_panel(c, x + 1, y + pos, 12, thumb, g_theme.face, 0);
}

void g_edit_set(GEdit *e, const char *text) { g_copy(e->text, text, sizeof(e->text)); e->cursor = g_len(e->text); }
int g_edit_key(GEdit *e, const GEvent *ev)
{
    int len = g_len(e->text);
    if (ev->type != EV_KEY) return 0;
    if (ev->code == BGX_KEY_LEFT) { e->cursor = g_utf8_prev(e->text, e->cursor); return 1; }
    if (ev->code == BGX_KEY_RIGHT) { e->cursor = g_utf8_next(e->text, len, e->cursor); return 1; }
    if (ev->code == 0xc7) { e->cursor = 0; return 1; }
    if (ev->code == 0xcf) { e->cursor = len; return 1; }
    if (ev->code == 0xd3 && e->cursor < len) {
        int next = g_utf8_next(e->text, len, e->cursor);
        for (int i = e->cursor; i <= len - (next - e->cursor); i++) e->text[i] = e->text[i + next - e->cursor];
        return 1;
    }
    if (ev->key == '\n') return 2;
    if (ev->code == BGX_KEY_ESC) return 3;
    if (ev->key == '\b') {
        if (!e->cursor) return 1;
        int prev = g_utf8_prev(e->text, e->cursor), gap = e->cursor - prev;
        for (int i = prev; i <= len - gap; i++) e->text[i] = e->text[i + gap];
        e->cursor = prev;
        return 1;
    }
    if (ev->key == 3 || ev->key == 24) {   /* Ctrl+C copies the field, Ctrl+X cuts it */
        if (len) g_clipboard_set(e->text, len);
        if (ev->key == 24) { e->text[0] = 0; e->cursor = 0; }
        return 1;
    }
    if (ev->key == 22) {   /* Ctrl+V */
        for (int i = 0; i < g_clipboard_len && g_clipboard[i] != '\n'; i++) {
            if (g_clipboard[i] == '\r') continue;
            if (len + 1 >= (int)sizeof(e->text)) break;
            for (int k = len; k >= e->cursor; k--) e->text[k + 1] = e->text[k];
            e->text[e->cursor++] = g_clipboard[i]; len++;
        }
        return 1;
    }
    if (ev->key >= 32 && ev->key != 127 && !(ev->key >= 0x100 && ev->key <= 0x108)) {
        char bytes[4]; int n = g_utf8_put(bytes, ev->key);
        if (len + n >= (int)sizeof(e->text)) return 1;
        for (int k = len; k >= e->cursor; k--) e->text[k + n] = e->text[k];
        for (int i = 0; i < n; i++) e->text[e->cursor + i] = bytes[i];
        e->cursor += n;
        return 1;
    }
    return 0;
}
void g_edit_draw(BgxCanvas *c, int x, int y, int width, const GEdit *e, int focused)
{
    g_panel(c, x, y, width, 22, g_theme.window, 1);
    /* columns before the cursor; scroll so it stays visible */
    int columns = (width - 8) / 8, before = 0, first = 0;
    for (int i = 0; i < e->cursor; i++) if (((unsigned char)e->text[i] & 0xc0) != 0x80) before++;
    int skip = before > columns - 1 ? before - columns + 1 : 0, seen = 0;
    while (e->text[first] && seen < skip) { first = g_utf8_next(e->text, g_len(e->text), first); seen++; }
    g_text_fit_raw(c, x + 4, y + 3, width - 8, e->text + first, g_theme.text);
    if (focused && (g_now() / 500) % 2 == 0) bgx_fill(c, x + 4 + (before - skip) * 8, y + 4, 1, 15, g_theme.text);
}

void g_list_draw(BgxCanvas *c, int x, int y, int w, int h, int count, int first, int selected,
                 GListText text, void *context, int focused)
{
    g_panel(c, x, y, w, h, g_theme.window, 1);
    int rows = (h - 4) / LIST_ROW;
    for (int r = 0; r < rows && first + r < count; r++) {
        int index = first + r, icon = -1, ry = y + 2 + r * LIST_ROW;
        const char *label = text(context, index, &icon);
        unsigned int fg = g_theme.text;
        if (index == selected) {
            bgx_fill(c, x + 2, ry, w - 18, LIST_ROW, focused ? g_theme.highlight : g_theme.shadow);
            fg = g_theme.highlight_text;
        }
        int tx = x + 6;
        if (icon >= 0) { g_icon(c, tx, ry + 1, icon, 16); tx += 20; }
        g_text_fit_raw(c, tx, ry + 1, w - 22 - (tx - x), label, fg);   /* rows are data: callbacks translate their own words */
    }
    g_scrollbar(c, x + w - 16, y + 2, h - 4, count, first, rows);
}
int g_list_scroll(int count, int first, int selected, int rows)
{
    if (selected < first) first = selected;
    if (selected >= first + rows) first = selected - rows + 1;
    if (first > count - rows) first = count - rows;
    return first < 0 ? 0 : first;
}
