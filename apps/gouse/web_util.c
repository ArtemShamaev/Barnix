/* Gouse Web: the page arena, strings, UTF-8 and URLs. */
#include "web.h"

/* ---------------------------------------------------------------- arena */
static unsigned char *arena_base;
static unsigned int arena_size, arena_used;
static unsigned char spare[8192];
int web_out_of_memory;
void web_arena(void *memory, unsigned int bytes)
{
    arena_base = memory; arena_size = bytes; arena_used = 0; web_out_of_memory = 0;
}
static void zero(void *p, unsigned int n)
{
    unsigned int *q = p;
    for (unsigned int i = 0; i < n / 4; i++) q[i] = 0;
    for (unsigned int i = n & ~3U; i < n; i++) ((unsigned char *)p)[i] = 0;
}
void *web_alloc_big(unsigned int bytes)
{
    bytes = (bytes + 7) & ~7U;
    if (!arena_base || bytes > arena_size - arena_used) { web_out_of_memory = 1; return 0; }
    void *p = arena_base + arena_used;
    arena_used += bytes;
    zero(p, bytes);
    return p;
}
/* A full arena must not crash the page: small requests then share a spare
 * block, and the page says it was cut short. */
void *web_alloc(unsigned int bytes)
{
    void *p = web_alloc_big(bytes);
    if (p) return p;
    if (bytes > sizeof(spare)) bytes = sizeof(spare);
    zero(spare, bytes);
    return spare;
}
/* Not zeroed: for moving data down inside the arena. */
void *web_take(unsigned int bytes)
{
    bytes = (bytes + 7) & ~7U;
    if (!arena_base || bytes > arena_size - arena_used) { web_out_of_memory = 1; return 0; }
    void *p = arena_base + arena_used;
    arena_used += bytes;
    return p;
}
unsigned int web_mark(void) { return arena_used; }
void web_release(unsigned int mark) { if (mark <= arena_used) arena_used = mark; }
unsigned int web_used(void) { return arena_used; }
char *web_strdup(const char *s, int length)
{
    if (length < 0) length = w_len(s);
    char *p = web_alloc((unsigned int)length + 1);
    if (p == (char *)spare && length >= (int)sizeof(spare)) length = sizeof(spare) - 1;
    for (int i = 0; i < length; i++) p[i] = s[i];
    p[length] = 0;
    return p;
}

/* ---------------------------------------------------------------- strings */
int w_len(const char *s) { int n = 0; if (s) while (s[n]) n++; return n; }
void w_copy(char *out, const char *s, int capacity)
{
    int n = 0;
    if (capacity <= 0) return;
    while (s && s[n] && n + 1 < capacity) { out[n] = s[n]; n++; }
    out[n] = 0;
}
void w_append(char *out, const char *s, int capacity)
{
    int n = w_len(out);
    if (n < capacity) w_copy(out + n, s, capacity - n);
}
int w_eq(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
int w_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int w_eqn_ci(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (w_lower((unsigned char)a[i]) != w_lower((unsigned char)b[i])) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}
int w_eq_ci(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && w_lower((unsigned char)*a) == w_lower((unsigned char)*b)) { a++; b++; }
    return w_lower((unsigned char)*a) == w_lower((unsigned char)*b);
}
int w_starts_ci(const char *s, const char *prefix)
{
    while (*prefix) { if (w_lower((unsigned char)*s) != w_lower((unsigned char)*prefix)) return 0; s++; prefix++; }
    return 1;
}
const char *w_find_ci(const char *s, const char *needle)
{
    if (!s) return 0;
    for (; *s; s++) if (w_starts_ci(s, needle)) return s;
    return 0;
}
int w_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
int w_itoa(char *out, int value)
{
    char digits[12]; int n = 0, k = 0;
    unsigned int v = value < 0 ? (unsigned int)-value : (unsigned int)value;
    do digits[n++] = (char)('0' + v % 10); while (v /= 10);
    if (value < 0) out[k++] = '-';
    while (n) out[k++] = digits[--n];
    out[k] = 0;
    return k;
}

/* ---------------------------------------------------------------- UTF-8 */
int w_utf8_decode(const unsigned char **p, const unsigned char *end)
{
    const unsigned char *s = *p;
    unsigned int c = *s++;
    int more = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (more < 0) { *p = s; return 0xfffd; }
    c &= more == 0 ? 0x7f : more == 1 ? 0x1f : more == 2 ? 0x0f : 0x07;
    for (int i = 0; i < more; i++) {
        if (s >= end || (*s & 0xc0) != 0x80) { *p = s; return 0xfffd; }
        c = c << 6 | (*s++ & 0x3f);
    }
    *p = s;
    return (int)c;
}
int w_utf8_put(char *out, unsigned int c)
{
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) { out[0] = (char)(0xc0 | c >> 6); out[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { out[0] = (char)(0xe0 | c >> 12); out[1] = (char)(0x80 | (c >> 6 & 63)); out[2] = (char)(0x80 | (c & 63)); return 3; }
    out[0] = (char)(0xf0 | c >> 18); out[1] = (char)(0x80 | (c >> 12 & 63)); out[2] = (char)(0x80 | (c >> 6 & 63)); out[3] = (char)(0x80 | (c & 63));
    return 4;
}

/* Legacy encodings named by <meta charset> become UTF-8. */
static const unsigned short cp1251[128] = {
    0x402,0x403,0x201a,0x453,0x201e,0x2026,0x2020,0x2021,0x20ac,0x2030,0x409,0x2039,0x40a,0x40c,0x40b,0x40f,
    0x452,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0xfffd,0x2122,0x459,0x203a,0x45a,0x45c,0x45b,0x45f,
    0xa0,0x40e,0x45e,0x408,0xa4,0x490,0xa6,0xa7,0x401,0xa9,0x404,0xab,0xac,0xad,0xae,0x407,
    0xb0,0xb1,0x406,0x456,0x491,0xb5,0xb6,0xb7,0x451,0x2116,0x454,0xbb,0x458,0x405,0x455,0x457};
static const unsigned short koi8r[64] = {   /* 0xc0..0xff */
    0x44e,0x430,0x431,0x446,0x434,0x435,0x444,0x433,0x445,0x438,0x439,0x43a,0x43b,0x43c,0x43d,0x43e,
    0x43f,0x44f,0x440,0x441,0x442,0x443,0x436,0x432,0x44c,0x44b,0x437,0x448,0x44d,0x449,0x447,0x44a,
    0x42e,0x410,0x411,0x426,0x414,0x415,0x424,0x413,0x425,0x418,0x419,0x41a,0x41b,0x41c,0x41d,0x41e,
    0x41f,0x42f,0x420,0x421,0x422,0x423,0x416,0x412,0x42c,0x42b,0x417,0x428,0x42d,0x429,0x427,0x42a};
/* The first "charset=" in text (a <meta> or a Content-Type header). */
int web_charset_of(const char *data, int length)
{
    int n = length < 4096 ? length : 4096;
    for (int i = 0; i + 8 < n; i++) {
        if (!w_eqn_ci(data + i, "charset", 7)) continue;
        int k = i + 7;
        while (k < n && (w_space(data[k]) || data[k] == '=' || data[k] == '"' || data[k] == '\'')) k++;
        if (w_eqn_ci(data + k, "windows-1251", 12) || w_eqn_ci(data + k, "cp1251", 6)) return WCS_1251;
        if (w_eqn_ci(data + k, "koi8", 4)) return WCS_KOI8R;
        if (w_eqn_ci(data + k, "iso-8859-1", 10) || w_eqn_ci(data + k, "windows-1252", 12) || w_eqn_ci(data + k, "latin1", 6)) return WCS_LATIN1;
        return WCS_UTF8;
    }
    return WCS_UTF8;
}
int w_valid_utf8(const char *data, int length)
{
    const unsigned char *p = (const unsigned char *)data, *end = p + length;
    while (p < end) {
        unsigned int c = *p;
        if (c < 0x80) { p++; continue; }
        int more = c >= 0xf0 && c < 0xf5 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : -1;
        if (more < 0 || end - p <= more) return 0;
        for (int i = 1; i <= more; i++) if ((p[i] & 0xc0) != 0x80) return 0;
        p += more + 1;
    }
    return 1;
}
char *web_to_utf8(const char *data, int length, int *out_length)
{
    return web_convert(data, length, web_charset_of(data, length), out_length);
}
char *web_convert(const char *data, int length, int kind, int *out_length)
{
    if (!kind) { *out_length = length; return (char *)data; }
    char *out = web_alloc_big((unsigned int)length * 3 + 1);
    if (!out) { *out_length = length; return (char *)data; }
    int n = 0;
    for (int i = 0; i < length; i++) {
        unsigned int c = (unsigned char)data[i];
        if (c >= 0x80) {
            if (kind == 1) c = c >= 0xc0 ? (c < 0xe0 ? 0x410 + c - 0xc0 : 0x430 + c - 0xe0) : cp1251[c - 0x80];
            else if (kind == 2) c = c >= 0xc0 ? koi8r[c - 0xc0] : c == 0xa3 ? 0x451 : c == 0xb3 ? 0x401 : 0xfffd;
        }
        n += w_utf8_put(out + n, c);
    }
    out[n] = 0;
    *out_length = n;
    return out;
}

/* ---------------------------------------------------------------- URLs */
static int scheme_length(const char *s)
{
    int n = 0;
    while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') || (n && ((s[n] >= '0' && s[n] <= '9') || s[n] == '+' || s[n] == '-' || s[n] == '.'))) n++;
    return n && s[n] == ':' ? n : 0;
}
/* Removes "." and ".." segments from a path that starts with '/'. */
static void normalize(char *path)
{
    char out[1024], tail[1024] = "";
    int n = 1, length = 0;
    while (path[length] && path[length] != '?' && path[length] != '#') length++;
    w_copy(tail, path + length, sizeof(tail));
    out[0] = '/';
    int at = 1;
    while (at <= length) {
        int end = at;
        while (end < length && path[end] != '/') end++;
        int size = end - at, last = end >= length;
        if (size == 1 && path[at] == '.') { }
        else if (size == 2 && path[at] == '.' && path[at + 1] == '.') {
            if (n > 1) { n--; while (n > 1 && out[n - 1] != '/') n--; }
        } else if (size || !last) {
            for (int i = 0; i < size && n < (int)sizeof(out) - 2; i++) out[n++] = path[at + i];
            if (!last && n < (int)sizeof(out) - 1) out[n++] = '/';
        }
        at = end + 1;
    }
    out[n] = 0;
    w_copy(path, out, 1024);
    w_append(path, tail, 1024);
}
void w_resolve_url(const char *base, const char *href, char *out, int capacity)
{
    char buffer[1024];
    while (w_space(*href)) href++;
    int hl = w_len(href);
    while (hl && w_space(href[hl - 1])) hl--;
    char h[1024];
    int k = 0;
    for (int i = 0; i < hl && k < (int)sizeof(h) - 4; i++) {   /* spaces inside URLs */
        if (href[i] == ' ') { h[k++] = '%'; h[k++] = '2'; h[k++] = '0'; }
        else if (href[i] != '\n' && href[i] != '\r' && href[i] != '\t') h[k++] = href[i];
    }
    h[k] = 0;
    if (scheme_length(h)) { w_copy(out, h, capacity); return; }
    int bs = scheme_length(base);
    if (!bs && base[0] == '/') {
        char local[1024] = "file://";
        w_append(local, base, sizeof(local));
        w_resolve_url(local, h, out, capacity);
        return;
    }
    if (!bs) { w_copy(out, h, capacity); return; }
    if (h[0] == '/' && h[1] == '/') {   /* scheme-relative */
        w_copy(buffer, base, bs + 2);
        w_append(buffer, h, sizeof(buffer));
        w_copy(out, buffer, capacity);
        return;
    }
    /* split base: scheme://authority path?query#fragment */
    const char *authority = base + bs + 1;
    int has_authority = authority[0] == '/' && authority[1] == '/';
    const char *path = has_authority ? authority + 2 : authority;
    if (has_authority) while (*path && *path != '/' && *path != '?' && *path != '#') path++;
    int prefix = (int)(path - base);
    if (!h[0]) { w_copy(out, base, capacity); for (char *q = out; *q; q++) if (*q == '#') { *q = 0; break; } return; }
    if (h[0] == '#') {
        w_copy(buffer, base, sizeof(buffer));
        for (char *q = buffer; *q; q++) if (*q == '#') { *q = 0; break; }
        w_append(buffer, h, sizeof(buffer));
        w_copy(out, buffer, capacity);
        return;
    }
    char p[1024];
    if (h[0] == '?') {
        int n = 0;
        while (path[n] && path[n] != '?' && path[n] != '#') n++;
        w_copy(p, path, n + 1 > (int)sizeof(p) ? (int)sizeof(p) : n + 1);
        if (!p[0]) w_copy(p, "/", sizeof(p));
        w_append(p, h, sizeof(p));
    } else if (h[0] == '/') w_copy(p, h, sizeof(p));
    else {
        int n = 0, slash = -1;
        while (path[n] && path[n] != '?' && path[n] != '#') { if (path[n] == '/') slash = n; n++; }
        if (slash < 0) { p[0] = '/'; p[1] = 0; }
        else w_copy(p, path, slash + 2 > (int)sizeof(p) ? (int)sizeof(p) : slash + 2);
        w_append(p, h, sizeof(p));
    }
    normalize(p);
    w_copy(buffer, base, prefix + 1 > (int)sizeof(buffer) ? (int)sizeof(buffer) : prefix + 1);
    w_append(buffer, p, sizeof(buffer));
    w_copy(out, buffer, capacity);
}
void w_url_encode(const char *s, char *out, int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *s && n + 4 < capacity; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '*') out[n++] = (char)c;
        else if (c == ' ') out[n++] = '+';
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}

/* ---------------------------------------------------------------- 64-bit division */
/* Barnix programs link without libgcc; GCC calls these for 64-bit / and %. */
#if defined(__i386__)
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *remainder);
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *remainder)
{
    unsigned long long q = 0, r = 0;
    if (!d) { if (remainder) *remainder = 0; return 0; }
    if (!(n >> 32) && !(d >> 32)) {
        unsigned int a = (unsigned int)n, b = (unsigned int)d;
        if (remainder) *remainder = a % b;
        return a / b;
    }
    for (int i = 63; i >= 0; i--) {
        r = r << 1 | (n >> i & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (remainder) *remainder = r;
    return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d);
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return __udivmoddi4(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d);
unsigned long long __umoddi3(unsigned long long n, unsigned long long d) { unsigned long long r; __udivmoddi4(n, d, &r); return r; }
long long __divdi3(long long n, long long d);
long long __divdi3(long long n, long long d)
{
    int negative = (n < 0) != (d < 0);
    unsigned long long q = __udivmoddi4(n < 0 ? -(unsigned long long)n : (unsigned long long)n, d < 0 ? -(unsigned long long)d : (unsigned long long)d, 0);
    return negative ? -(long long)q : (long long)q;
}
long long __moddi3(long long n, long long d);
long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    __udivmoddi4(n < 0 ? -(unsigned long long)n : (unsigned long long)n, d < 0 ? -(unsigned long long)d : (unsigned long long)d, &r);
    return n < 0 ? -(long long)r : (long long)r;
}
long long __divmoddi4(long long n, long long d, long long *remainder);
long long __divmoddi4(long long n, long long d, long long *remainder)
{
    long long q = __divdi3(n, d);
    if (remainder) *remainder = n - q * d;
    return q;
}
#endif
