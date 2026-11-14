/* Gouse Web: CSS values. Lengths (px, em, rem, %, pt, vw, vh, ch...),
 * calc(), min(), max(), clamp() and colors (names, #hex, rgb(), hsl()). */
#include "web.h"

WDoc *w_doc;

/* ---------------------------------------------------------------- numbers */
/* A CSS number at *s in 1/1000 units; 0 when there is none. */
static int number(const char **s, int *value)
{
    const char *p = *s;
    int sign = 1, any = 0;
    long long v = 0;
    if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { if (v < 100000000) v = v * 10 + (*p - '0'); p++; any = 1; }
    v *= 1000;
    if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
        p++;
        int scale = 100;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * scale; scale /= 10; p++; any = 1; }
    }
    if (!any) return 0;
    if ((*p == 'e' || *p == 'E') && (p[1] == '-' || p[1] == '+' || (p[1] >= '0' && p[1] <= '9')) && !(p[1] == 'm' || p[1] == 'x')) {
        const char *q = p + 1;
        int es = 1, e = 0;
        if (*q == '-' || *q == '+') { if (*q == '-') es = -1; q++; }
        while (*q >= '0' && *q <= '9') { e = e * 10 + (*q - '0'); q++; }
        for (int i = 0; i < e && i < 9; i++) v = es > 0 ? v * 10 : v / 10;
        p = q;
    }
    *value = (int)(sign * (v > 2000000000 ? 2000000000 : v));
    *s = p;
    return 1;
}
static int unit_is(const char *s, const char *unit)
{
    int n = w_len(unit);
    if (!w_eqn_ci(s, unit, n)) return 0;
    char c = s[n];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '%');
}

/* ---------------------------------------------------------------- lengths */
typedef struct { int px1000, pct1000, number, has_unit, ok; } Term;   /* px * 1000, % * 1000 */
static int font_px, root_px;
static int zoom(int v) { return w_doc && w_doc->zoom != 100 ? (int)((long long)v * w_doc->zoom / 100) : v; }
static Term sum(const char **s);
static void skip(const char **s) { while (w_space(**s)) (*s)++; }
static Term primary(const char **s)
{
    Term t = {0, 0, 0, 0, 1};
    skip(s);
    if (**s == '(') {
        (*s)++;
        t = sum(s);
        skip(s);
        if (**s == ')') (*s)++;
        return t;
    }
    if (w_starts_ci(*s, "calc(")) { *s += 5; t = sum(s); skip(s); if (**s == ')') (*s)++; return t; }
    if (w_starts_ci(*s, "var(")) { t.ok = 0; return t; }
    int v;
    if (!number(s, &v)) { t.ok = 0; return t; }
    const char *u = *s;
    long long px = 0;
    int vw = w_doc ? w_doc->width : 800, vh = w_doc ? w_doc->viewport_h : 600;
    t.has_unit = 1;
    if (unit_is(u, "px")) { px = zoom(v); *s += 2; }
    else if (*u == '%') { t.pct1000 = v; (*s)++; return t; }
    else if (unit_is(u, "em")) { px = (long long)v * font_px; *s += 2; }
    else if (unit_is(u, "rem")) { px = (long long)v * root_px; *s += 3; }
    else if (unit_is(u, "ex") || unit_is(u, "ch")) { px = (long long)v * font_px / 2; *s += 2; }
    else if (unit_is(u, "pt")) { px = zoom(v) * 4LL / 3; *s += 2; }
    else if (unit_is(u, "pc")) { px = zoom(v) * 16LL; *s += 2; }
    else if (unit_is(u, "in")) { px = zoom(v) * 96LL; *s += 2; }
    else if (unit_is(u, "cm")) { px = zoom(v) * 378LL / 10; *s += 2; }
    else if (unit_is(u, "mm")) { px = zoom(v) * 378LL / 100; *s += 2; }
    else if (unit_is(u, "q")) { px = zoom(v) * 378LL / 400; *s += 1; }
    else if (unit_is(u, "vw") || unit_is(u, "svw") || unit_is(u, "lvw") || unit_is(u, "dvw")) { px = (long long)v * vw / 100; while (**s && **s != 'w') (*s)++; (*s)++; }
    else if (unit_is(u, "vh") || unit_is(u, "svh") || unit_is(u, "lvh") || unit_is(u, "dvh")) { px = (long long)v * vh / 100; while (**s && **s != 'h') (*s)++; (*s)++; }
    else if (unit_is(u, "vmin")) { px = (long long)v * (vw < vh ? vw : vh) / 100; *s += 4; }
    else if (unit_is(u, "vmax")) { px = (long long)v * (vw > vh ? vw : vh) / 100; *s += 4; }
    else if (unit_is(u, "lh")) { px = (long long)v * font_px * 12 / 10; *s += 2; }
    else if ((*u >= 'a' && *u <= 'z') || (*u >= 'A' && *u <= 'Z')) { t.ok = 0; return t; }   /* deg, s, fr... */
    else { t.has_unit = 0; t.number = v; px = v; }
    t.px1000 = (int)(px > 2000000000 ? 2000000000 : px < -2000000000 ? -2000000000 : px);
    return t;
}
static Term product(const char **s)
{
    Term a = primary(s);
    for (;;) {
        skip(s);
        char op = **s;
        if (op != '*' && op != '/') return a;
        (*s)++;
        Term b = primary(s);
        if (!b.ok) { a.ok = 0; return a; }
        if (op == '*') {
            if (!b.has_unit) { a.px1000 = (int)((long long)a.px1000 * b.number / 1000); a.pct1000 = (int)((long long)a.pct1000 * b.number / 1000); }
            else if (!a.has_unit) { int n = a.number; a = b; a.px1000 = (int)((long long)b.px1000 * n / 1000); a.pct1000 = (int)((long long)b.pct1000 * n / 1000); }
        } else if (!b.has_unit && b.number) {
            a.px1000 = (int)((long long)a.px1000 * 1000 / b.number); a.pct1000 = (int)((long long)a.pct1000 * 1000 / b.number);
            if (!a.has_unit) a.number = (int)((long long)a.number * 1000 / b.number);
        }
    }
}
static Term sum(const char **s)
{
    Term a = product(s);
    for (;;) {
        skip(s);
        char op = **s;
        if ((op != '+' && op != '-') || !w_space((*s)[1])) return a;
        (*s)++;
        Term b = product(s);
        if (!b.ok) { a.ok = 0; return a; }
        if (op == '+') { a.px1000 += b.px1000; a.pct1000 += b.pct1000; a.number += b.number; }
        else { a.px1000 -= b.px1000; a.pct1000 -= b.pct1000; a.number -= b.number; }
        a.has_unit |= b.has_unit;
    }
}
static WLen len_of(Term t)
{
    WLen l = {WL_LEN, 0, 0, 0};
    l.px = t.px1000 >= 0 ? (t.px1000 + 500) / 1000 : -((-t.px1000 + 500) / 1000);
    l.pct = t.pct1000 / 10;
    return l;
}
static WLen function_len(const char **s, int kind, int count);
WLen w_parse_len(const char *s, int font_size, int root_size, int *ok)
{
    WLen none = {WL_AUTO, 0, 0, 0};
    font_px = font_size; root_px = root_size;
    if (ok) *ok = 0;
    if (!s) return none;
    while (w_space(*s)) s++;
    if (w_starts_ci(s, "auto")) { if (ok) *ok = 1; return none; }
    if (w_starts_ci(s, "none")) { WLen l = {WL_NONE, 0, 0, 0}; if (ok) *ok = 1; return l; }
    if (w_starts_ci(s, "min-content") || w_starts_ci(s, "max-content") || w_starts_ci(s, "fit-content")) {
        WLen l = {WL_CONTENT, 0, 0, 0};
        if (w_starts_ci(s, "min-content")) l.px = 1;
        if (ok) *ok = 1;
        return l;
    }
    if (w_starts_ci(s, "min(")) { s += 4; WLen l = function_len(&s, WL_MIN, 2); if (ok) *ok = l.kind != WL_AUTO; return l; }
    if (w_starts_ci(s, "max(")) { s += 4; WLen l = function_len(&s, WL_MAX, 2); if (ok) *ok = l.kind != WL_AUTO; return l; }
    if (w_starts_ci(s, "clamp(")) { s += 6; WLen l = function_len(&s, WL_CLAMP, 3); if (ok) *ok = l.kind != WL_AUTO; return l; }
    const char *p = s;
    Term t = sum(&p);
    if (!t.ok || (t.has_unit == 0 && t.number != 0)) return none;   /* unitless non-zero */
    if (ok) *ok = 1;
    return len_of(t);
}
/* Arguments separated by commas, each a sum. */
static WLen function_len(const char **s, int kind, int count)
{
    WLen l = {(unsigned char)kind, 0, 0, 0};
    l.args = web_alloc(sizeof(WLen) * 3);
    int n = 0;
    while (n < count) {
        skip(s);
        if (w_starts_ci(*s, "min(")) { *s += 4; l.args[n] = function_len(s, WL_MIN, 2); }
        else if (w_starts_ci(*s, "max(")) { *s += 4; l.args[n] = function_len(s, WL_MAX, 2); }
        else { Term t = sum(s); if (!t.ok) { WLen bad = {WL_AUTO, 0, 0, 0}; return bad; } l.args[n] = len_of(t); }
        n++;
        skip(s);
        if (**s == ',') (*s)++;
        else break;
    }
    /* min()/max() of more arguments: keep the first two */
    while (**s && **s != ')') (*s)++;
    if (**s == ')') (*s)++;
    if (n < count) { WLen bad = {WL_AUTO, 0, 0, 0}; return bad; }
    return l;
}
int w_resolve(const WLen *l, int containing, int fallback)
{
    switch (l->kind) {
    case WL_LEN: return l->px + (int)((long long)l->pct * containing / 10000);
    case WL_MIN: { int a = w_resolve(&l->args[0], containing, fallback), b = w_resolve(&l->args[1], containing, fallback); return a < b ? a : b; }
    case WL_MAX: { int a = w_resolve(&l->args[0], containing, fallback), b = w_resolve(&l->args[1], containing, fallback); return a > b ? a : b; }
    case WL_CLAMP: {
        int lo = w_resolve(&l->args[0], containing, fallback), v = w_resolve(&l->args[1], containing, fallback), hi = w_resolve(&l->args[2], containing, fallback);
        if (v > hi) v = hi;
        if (v < lo) v = lo;
        return v;
    }
    }
    return fallback;
}

/* ---------------------------------------------------------------- colors */
static const struct { const char *name; unsigned int rgb; } color_names[] = {
    {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000}, {"blanchedalmond", 0xffebcd},
    {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a}, {"burlywood", 0xdeb887},
    {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e}, {"coral", 0xff7f50},
    {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c}, {"cyan", 0x00ffff},
    {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b}, {"darkgray", 0xa9a9a9},
    {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b}, {"darkmagenta", 0x8b008b},
    {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc}, {"darkred", 0x8b0000},
    {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b}, {"darkslategray", 0x2f4f4f},
    {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493},
    {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"dodgerblue", 0x1e90ff},
    {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0}, {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff},
    {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff}, {"gold", 0xffd700}, {"goldenrod", 0xdaa520},
    {"gray", 0x808080}, {"grey", 0x808080}, {"green", 0x008000}, {"greenyellow", 0xadff2f}, {"honeydew", 0xf0fff0},
    {"hotpink", 0xff69b4}, {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c},
    {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd},
    {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080}, {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2},
    {"lightgray", 0xd3d3d3}, {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a}, {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899}, {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0}, {"lime", 0x00ff00},
    {"limegreen", 0x32cd32}, {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000},
    {"mediumaquamarine", 0x66cdaa}, {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db},
    {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a},
    {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970}, {"mintcream", 0xf5fffa},
    {"mistyrose", 0xffe4e1}, {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead}, {"navy", 0x000080},
    {"oldlace", 0xfdf5e6}, {"olive", 0x808000}, {"olivedrab", 0x6b8e23}, {"orange", 0xffa500},
    {"orangered", 0xff4500}, {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98},
    {"paleturquoise", 0xafeeee}, {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5}, {"peachpuff", 0xffdab9},
    {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd}, {"powderblue", 0xb0e0e6}, {"purple", 0x800080},
    {"rebeccapurple", 0x663399}, {"red", 0xff0000}, {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1},
    {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072}, {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57},
    {"seashell", 0xfff5ee}, {"sienna", 0xa0522d}, {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb},
    {"slateblue", 0x6a5acd}, {"slategray", 0x708090}, {"slategrey", 0x708090}, {"snow", 0xfffafa},
    {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4}, {"tan", 0xd2b48c}, {"teal", 0x008080}, {"thistle", 0xd8bfd8},
    {"tomato", 0xff6347}, {"turquoise", 0x40e0d0}, {"violet", 0xee82ee}, {"wheat", 0xf5deb3}, {"white", 0xffffff},
    {"whitesmoke", 0xf5f5f5}, {"yellow", 0xffff00}, {"yellowgreen", 0x9acd32}, {"canvas", 0xffffff},
    {"canvastext", 0x000000}, {"linktext", 0x0000ee}, {"buttonface", 0xefefef}, {"buttontext", 0x000000},
    {"field", 0xffffff}, {"fieldtext", 0x000000}, {"highlight", 0x3875d7}, {"highlighttext", 0xffffff},
    {"graytext", 0x808080}, {"window", 0xffffff}, {"windowtext", 0x000000},
};
static int hex_digit(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static unsigned int from_hex(const char *s, int n, int *ok)
{
    int v[8];
    for (int i = 0; i < n; i++) if ((v[i] = hex_digit(s[i])) < 0) { *ok = 0; return 0; }
    *ok = 1;
    if (n == 3) return 0xff000000u | (unsigned)(v[0] * 17) << 16 | (unsigned)(v[1] * 17) << 8 | (unsigned)(v[2] * 17);
    if (n == 4) return (unsigned)(v[3] * 17) << 24 | (unsigned)(v[0] * 17) << 16 | (unsigned)(v[1] * 17) << 8 | (unsigned)(v[2] * 17);
    if (n == 6) return 0xff000000u | (unsigned)(v[0] << 4 | v[1]) << 16 | (unsigned)(v[2] << 4 | v[3]) << 8 | (unsigned)(v[4] << 4 | v[5]);
    if (n == 8) return (unsigned)(v[6] << 4 | v[7]) << 24 | (unsigned)(v[0] << 4 | v[1]) << 16 | (unsigned)(v[2] << 4 | v[3]) << 8 | (unsigned)(v[4] << 4 | v[5]);
    *ok = 0;
    return 0;
}
/* Arguments of rgb()/hsl(): numbers (x1000) and whether each was a percentage. */
static int arguments(const char *s, int *values, int *percent, int max)
{
    int n = 0;
    while (*s && *s != ')' && n < max) {
        while (w_space(*s) || *s == ',' || *s == '/') s++;
        if (*s == ')' || !*s) break;
        if (w_starts_ci(s, "none")) { values[n] = 0; percent[n++] = 0; s += 4; continue; }
        if (!number(&s, &values[n])) return -1;
        percent[n] = *s == '%';
        if (*s == '%') s++;
        while ((*s >= 'a' && *s <= 'z')) s++;   /* deg, turn */
        n++;
    }
    return n;
}
static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int hue_part(int p, int q, int t)   /* all x1000, t in 0..6000 */
{
    if (t < 0) t += 6000;
    if (t >= 6000) t -= 6000;
    if (t < 1000) return p + (q - p) * t / 1000;
    if (t < 3000) return q;
    if (t < 4000) return p + (q - p) * (4000 - t) / 1000;
    return p;
}
unsigned int w_parse_color(const char *s, unsigned int current, int *ok)
{
    *ok = 0;
    if (!s) return 0;
    while (w_space(*s)) s++;
    if (*s == '#') {
        int n = 0;
        while (hex_digit(s[1 + n]) >= 0) n++;
        return from_hex(s + 1, n, ok);
    }
    if (w_starts_ci(s, "rgb")) {
        const char *p = s + 3;
        if (*p == 'a' || *p == 'A') p++;
        if (*p != '(') return 0;
        int v[4], pc[4], n = arguments(p + 1, v, pc, 4);
        if (n < 3) return 0;
        int c[3];
        for (int i = 0; i < 3; i++) c[i] = clamp255(pc[i] ? v[i] * 255 / 100000 : (v[i] + 500) / 1000);
        int a = n > 3 ? clamp255(pc[3] ? v[3] * 255 / 100000 : v[3] * 255 / 1000) : 255;
        *ok = 1;
        return (unsigned)a << 24 | (unsigned)c[0] << 16 | (unsigned)c[1] << 8 | (unsigned)c[2];
    }
    if (w_starts_ci(s, "hsl")) {
        const char *p = s + 3;
        if (*p == 'a' || *p == 'A') p++;
        if (*p != '(') return 0;
        int v[4], pc[4], n = arguments(p + 1, v, pc, 4);
        if (n < 3) return 0;
        int h = v[0] / 60 % 6000; if (h < 0) h += 6000;   /* sixths of a turn, x1000 */
        int sat = v[1] / 100, light = v[2] / 100;          /* x1000 */
        if (sat > 1000) sat = 1000;
        if (light > 1000) light = 1000;
        int q = light < 500 ? light * (1000 + sat) / 1000 : light + sat - light * sat / 1000, p2 = 2 * light - q;
        int r = hue_part(p2, q, h + 2000), g = hue_part(p2, q, h), b = hue_part(p2, q, h - 2000);
        int a = n > 3 ? clamp255(pc[3] ? v[3] * 255 / 100000 : v[3] * 255 / 1000) : 255;
        *ok = 1;
        return (unsigned)a << 24 | (unsigned)clamp255(r * 255 / 1000) << 16 | (unsigned)clamp255(g * 255 / 1000) << 8 | (unsigned)clamp255(b * 255 / 1000);
    }
    if (w_starts_ci(s, "transparent")) { *ok = 1; return 0; }
    if (w_starts_ci(s, "currentcolor") || w_starts_ci(s, "inherit")) { *ok = 1; return current; }
    if (w_starts_ci(s, "color-mix(") || w_starts_ci(s, "light-dark(")) {   /* the first color is near enough */
        const char *p = s;
        while (*p && *p != ',' && *p != '(') p++;
        if (*p == '(') p++;
        if (w_starts_ci(s, "color-mix(")) { while (*p && *p != ',') p++; if (*p) p++; }
        return w_parse_color(p, current, ok);
    }
    int length = 0;
    while ((s[length] >= 'a' && s[length] <= 'z') || (s[length] >= 'A' && s[length] <= 'Z')) length++;
    if (!length || length > 24) return 0;
    for (unsigned int i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++)
        if (w_len(color_names[i].name) == length && w_eqn_ci(color_names[i].name, s, length)) { *ok = 1; return 0xff000000u | color_names[i].rgb; }
    /* legacy attribute colors without '#' */
    if (length == 6 || length == 3) { unsigned int c = from_hex(s, length, ok); if (*ok) return c; }
    return 0;
}
