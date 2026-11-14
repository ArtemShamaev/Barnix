/* Calculator: decimal fixed point (six places) on 64-bit integers, no FPU. */
#include "gouse.h"

#define SCALE 1000000ULL
#define LIMIT 1000000000000ULL   /* 10^12 */
typedef long long Fix;
typedef struct {
    char entry[24];      /* digits being typed, empty when showing value */
    Fix value, stored, memory;
    int op, error, fresh;
} State;
#define S ((State *)w->state)
static const char *const keys[5][5] = {
    {"7", "8", "9", "/", "C"}, {"4", "5", "6", "*", "CE"}, {"1", "2", "3", "-", "sqrt"},
    {"0", "+/-", ".", "+", "%"}, {"MC", "MR", "M+", "<-", "="}};

static unsigned long long magnitude(Fix v) { return v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v; }
static int multiply(Fix a, Fix b, Fix *out)
{
    unsigned long long x = magnitude(a), y = magnitude(b), xr, yr;
    unsigned long long xi = g_udiv64(x, SCALE, &xr), yi = g_udiv64(y, SCALE, &yr);
    if (xi && yi && xi > g_udiv64(LIMIT, yi, 0)) return -1;
    unsigned long long r = xi * yi * SCALE + xi * yr + xr * yi + g_udiv64(xr * yr, SCALE, 0);
    if (r >= LIMIT * SCALE) return -1;
    *out = (a < 0) != (b < 0) ? -(Fix)r : (Fix)r;
    return 0;
}
static int divide(Fix a, Fix b, Fix *out)
{
    if (!b) return -1;
    unsigned long long x = magnitude(a), y = magnitude(b), r;
    unsigned long long q = g_udiv64(x, y, &r);
    if (q >= LIMIT) return -1;
    unsigned long long fraction = 0;
    for (int i = 0; i < 6; i++) {   /* long division, one decimal digit at a time */
        r *= 10;
        unsigned long long d = g_udiv64(r, y, &r);
        fraction = fraction * 10 + d;
    }
    unsigned long long v = q * SCALE + fraction;
    *out = (a < 0) != (b < 0) ? -(Fix)v : (Fix)v;
    return 0;
}
static unsigned long long isqrt64(unsigned long long n)
{
    unsigned long long r = 0, bit = 1ULL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}
static void show(const State *s, char *out, int capacity)
{
    if (s->error) { g_copy(out, "Error", capacity); return; }
    if (s->entry[0]) { g_copy(out, s->entry, capacity); return; }
    unsigned long long v = magnitude(s->value), f;
    unsigned long long i = g_udiv64(v, SCALE, &f);
    char digits[32]; int n = 0;
    do { unsigned long long d; i = g_udiv64(i, 10, &d); digits[n++] = '0' + (int)d; } while (i);
    int k = 0;
    if (s->value < 0) out[k++] = '-';
    while (n && k < capacity - 1) out[k++] = digits[--n];
    if (f) {
        char frac[7];
        for (int j = 5; j >= 0; j--) { unsigned long long d; f = g_udiv64(f, 10, &d); frac[j] = '0' + (int)d; }
        int last = 5; while (last >= 0 && frac[last] == '0') last--;
        if (k < capacity - 1) out[k++] = '.';
        for (int j = 0; j <= last && k < capacity - 1; j++) out[k++] = frac[j];
    }
    out[k] = 0;
}
static Fix parse(const char *s)
{
    int negative = *s == '-';
    if (negative) s++;
    static const unsigned int place[6] = {100000, 10000, 1000, 100, 10, 1};
    unsigned long long i = 0, f = 0;
    while (*s >= '0' && *s <= '9') i = i * 10 + (*s++ - '0');
    if (*s == '.') { s++; for (int d = 0; d < 6 && *s >= '0' && *s <= '9'; d++) f += (unsigned int)(*s++ - '0') * place[d]; }
    Fix v = (Fix)(i * SCALE + f);
    return negative ? -v : v;
}
static Fix current(State *s) { return s->entry[0] ? parse(s->entry) : s->value; }
static void apply(State *s)
{
    Fix a = s->stored, b = current(s), r = b;
    int failed = 0;
    switch (s->op) {
    case '+': r = a + b; break;
    case '-': r = a - b; break;
    case '*': failed = multiply(a, b, &r); break;
    case '/': failed = divide(a, b, &r); break;
    }
    if (failed || magnitude(r) >= LIMIT * SCALE) { s->error = 1; r = 0; }
    s->value = r; s->entry[0] = 0; s->op = 0;
}
static void press_key(State *s, const char *k);
/* The goose honks when a key leads to an error. */
static void press(State *s, const char *k)
{
    int before = s->error;
    press_key(s, k);
    if (s->error && !before) g_sound(G_SOUND_ERROR);
}
static void press_key(State *s, const char *k)
{
    if (s->error && !g_equal(k, "C")) return;
    int len = g_len(s->entry);
    if (k[0] >= '0' && k[0] <= '9' && !k[1]) {
        if (s->fresh) { s->entry[0] = 0; len = 0; s->fresh = 0; }
        if (len < 15) { if (len == 1 && s->entry[0] == '0') len = 0; s->entry[len] = k[0]; s->entry[len + 1] = 0; }
    } else if (g_equal(k, ".")) {
        if (s->fresh || !len) { g_copy(s->entry, "0", sizeof(s->entry)); s->fresh = 0; len = 1; }
        for (int i = 0; i < len; i++) if (s->entry[i] == '.') return;
        if (len < 15) { s->entry[len] = '.'; s->entry[len + 1] = 0; }
    } else if (g_equal(k, "C")) { s->entry[0] = 0; s->value = s->stored = 0; s->op = 0; s->error = 0; }
    else if (g_equal(k, "CE")) s->entry[0] = 0;
    else if (g_equal(k, "<-")) { if (len) s->entry[len - 1] = 0; }
    else if (g_equal(k, "+/-")) {
        if (s->entry[0]) {
            if (s->entry[0] == '-') for (int i = 0; i <= len; i++) s->entry[i] = s->entry[i + 1];
            else if (len < 15) { for (int i = len; i >= 0; i--) s->entry[i + 1] = s->entry[i]; s->entry[0] = '-'; }
        } else s->value = -s->value;
    } else if (g_equal(k, "+") || g_equal(k, "-") || g_equal(k, "*") || g_equal(k, "/")) {
        if (s->op && s->entry[0]) apply(s); else if (s->entry[0]) { s->value = parse(s->entry); s->entry[0] = 0; }
        s->stored = s->value; s->op = k[0]; s->fresh = 1;
    } else if (g_equal(k, "=")) { if (s->op) apply(s); else if (s->entry[0]) { s->value = parse(s->entry); s->entry[0] = 0; } }
    else if (g_equal(k, "%")) {
        Fix p, base = s->op ? s->stored : (Fix)SCALE;
        if (multiply(base, current(s), &p) || divide(p, 100 * (Fix)SCALE, &p)) s->error = 1;
        else { s->entry[0] = 0; s->value = p; }
    } else if (g_equal(k, "sqrt")) {
        Fix v = current(s);
        if (v < 0) { s->error = 1; return; }
        unsigned long long u = (unsigned long long)v;
        s->value = (Fix)(u < 9000000000000ULL ? isqrt64(u * SCALE) : isqrt64(u) * 1000);
        s->entry[0] = 0;
    } else if (g_equal(k, "MC")) s->memory = 0;
    else if (g_equal(k, "MR")) { s->value = s->memory; s->entry[0] = 0; }
    else if (g_equal(k, "M+")) s->memory += current(s);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    char text[32]; show(s, text, sizeof(text));
    g_panel(c, 8, 8, w->w - 16, 40, 0xd8f0d0, 1);
    int tw = bgx_text_width(text, 2);
    if (tw > w->w - 28) bgx_text(c, w->w - 16 - bgx_text_width(text, 1), 20, text, 0x103010, 1);
    else bgx_text(c, w->w - 16 - tw, 12, text, 0x103010, 2);
    if (s->memory) g_text(c, 14, 10, "M", 0x103010);
    if (s->op) { char op[2] = {(char)s->op, 0}; g_text(c, 14, 28, op, 0x103010); }
    int bw = (w->w - 16 - 4 * 6) / 5, bh = (w->h - 60 - 4 * 6) / 5;
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 5; k++)
            g_button(w, c, r * 5 + k + 1, 8 + k * (bw + 6), 56 + r * (bh + 6), bw, bh, keys[r][k]);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_COMMAND && e->id >= 1 && e->id <= 25) press(s, keys[(e->id - 1) / 5][(e->id - 1) % 5]);
    else if (e->type == EV_KEY) {
        char k[2] = {(char)e->key, 0};
        if (e->key == '\n' || e->key == '=') press(s, "=");
        else if (e->key == '\b') press(s, "<-");
        else if (e->code == BGX_KEY_ESC) press(s, "C");
        else if (e->code == 0xd3) press(s, "CE");
        else if (e->key == ',') press(s, ".");
        else if ((e->key >= '0' && e->key <= '9') || e->key == '.' || e->key == '+' || e->key == '-' || e->key == '*' || e->key == '/' || e->key == '%') press(s, k);
        else if (e->key == 'r' || e->key == 'R') press(s, "sqrt");
    } else if (e->type == EV_MENU && e->id == 1) {
        char text[32]; show(s, text, sizeof(text)); g_clipboard_set(text, g_len(text));
    } else if (e->type == EV_MENU && e->id == 2) {
        char text[24]; int n = 0;
        for (int i = 0; i < g_clipboard_len && n < 15; i++)
            if ((g_clipboard[i] >= '0' && g_clipboard[i] <= '9') || g_clipboard[i] == '.' || (g_clipboard[i] == '-' && !n)) text[n++] = g_clipboard[i];
        text[n] = 0;
        if (n) { g_copy(s->entry, text, sizeof(s->entry)); s->fresh = 0; }
    }
}
static const GMenu menus[] = {{"Edit", {{"Copy result", 1}, {"Paste number", 2}}}};
const GApp app_calculator = {"Calculator", "Add, subtract, multiply, divide", ICON_CALCULATOR, 260, 300, 220, 260,
                             APP_RESIZABLE, menus, 1, sizeof(State), 0, event, 0, paint, 0};
