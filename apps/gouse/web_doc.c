/* Gouse Web: a document's resources, relayout, painting, hit testing,
 * find, fragments and form submission. */
#include "web.h"

/* ---------------------------------------------------------------- resources */
static int base64_value(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}
/* data: URLs (base64 or percent-encoded) into the arena. */
static char *data_url(const char *url, int *length)
{
    const char *comma = url;
    while (*comma && *comma != ',') comma++;
    if (!*comma) return 0;
    int base64 = 0;
    for (const char *p = url; p < comma; p++) if (w_starts_ci(p, ";base64")) base64 = 1;
    const char *s = comma + 1;
    int n = w_len(s);
    char *out = web_alloc_big((unsigned)n + 1);
    if (!out) return 0;
    int k = 0;
    if (base64) {
        unsigned int acc = 0; int bits = 0;
        for (int i = 0; i < n; i++) {
            int v = base64_value((unsigned char)s[i]);
            if (v < 0) continue;
            acc = acc << 6 | (unsigned)v; bits += 6;
            if (bits >= 8) { bits -= 8; out[k++] = (char)(acc >> bits); }
        }
    } else {
        for (int i = 0; i < n; i++) {
            if (s[i] == '%' && i + 2 < n) {
                int h = base64_value(0);
                (void)h;
                int a = s[i + 1], b = s[i + 2];
                int va = a >= '0' && a <= '9' ? a - '0' : (a | 32) - 'a' + 10, vb = b >= '0' && b <= '9' ? b - '0' : (b | 32) - 'a' + 10;
                out[k++] = (char)(va * 16 + vb); i += 2;
            } else out[k++] = s[i];
        }
    }
    out[k] = 0;
    *length = k;
    return out;
}
static int media_screen(const char *media)
{
    if (!media || !media[0]) return 1;
    return w_find_ci(media, "screen") || w_find_ci(media, "all") || (!w_find_ci(media, "print") && !w_find_ci(media, "speech"));
}
static const char *image_source(const WNode *n)
{
    const char *src = w_attr(n, "src");
    const char *lazy = w_attr(n, "data-src");
    if (lazy && (!src || w_starts_ci(src, "data:image/gif") || w_starts_ci(src, "data:image/svg"))) src = lazy;
    if (!src || !src[0]) {
        const char *set = w_attr(n, "srcset");
        if (!set) set = w_attr(n, "data-srcset");
        if (set) {   /* the first candidate */
            static char first[512];
            int k = 0;
            while (*set && w_space(*set)) set++;
            while (*set && !w_space(*set) && *set != ',' && k < 511) first[k++] = *set++;
            first[k] = 0;
            src = first;
        }
    }
    return src;
}
void web_load_resources(WDoc *d, int images)
{
    if (!d->js) web_js_load(d);
    w_doc = d;
    web_css_reset(d);
    int sheets = 0;
    for (WNode *n = d->root; n;) {
        if (n->type == WN_ELEMENT) {
            if (n->tag == T_STYLE && n->first && media_screen(w_attr(n, "media")))
                web_add_stylesheet(d, n->first->text, n->first->length, d->base, 0);
            else if (n->tag == T_LINK && sheets < 24) {
                const char *rel = w_attr(n, "rel"), *href = w_attr(n, "href");
                if (rel && href && w_find_ci(rel, "stylesheet") && !w_find_ci(rel, "alternate") && media_screen(w_attr(n, "media"))) {
                    char url[512];
                    w_resolve_url(d->base, href, url, sizeof(url));
                    web_progress("Loading style sheet...");
                    WebResource r;
                    if (!web_fetch(url, &r, WEB_RESOURCE_MAX, 0, 0) && r.data) { web_add_stylesheet(d, r.data, r.length, r.url[0] ? r.url : url, 0); sheets++; }
                }
            }
        }
        if (n->first && n->tag != T_STYLE && n->tag != T_SCRIPT) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    if (images) {
        int count = 0, total = 0;
        for (WNode *n = d->root; n && count < 80 && total < (12 << 20) && !web_out_of_memory;) {
            if (n->type == WN_ELEMENT && (n->tag == T_IMG || (n->tag == T_INPUT && w_attr(n, "type") && w_eq_ci(w_attr(n, "type"), "image")))) {
                const char *src = image_source(n);
                if (src && src[0]) {
                    char url[512];
                    w_resolve_url(d->base, src, url, sizeof(url));
                    /* the same picture twice: decode once */
                    WImage *same = 0;
                    for (WNode *m = d->root; m && m != n;) {
                        if (m->type == WN_ELEMENT && m->image && (m->tag == T_IMG || m->tag == T_INPUT)) {
                            const char *ms = image_source(m);
                            char murl[512];
                            if (ms) { w_resolve_url(d->base, ms, murl, sizeof(murl)); if (w_eq(murl, url)) { same = m->image; break; } }
                        }
                        if (m->first) { m = m->first; continue; }
                        while (m && !m->next) m = m->parent;
                        if (m) m = m->next;
                    }
                    if (same) n->image = same;
                    else if (w_starts_ci(url, "data:")) {
                        int length;
                        char *data = data_url(url, &length);
                        if (data) n->image = web_png((const unsigned char *)data, length);
                    } else if (!w_find_ci(url, ".svg") && !w_find_ci(url, ".gif") && !w_find_ci(url, ".webp") && !w_find_ci(url, ".jpg") && !w_find_ci(url, ".jpeg")) {
                        char text[96] = "Loading image ";
                        char number[12];
                        w_itoa(number, count + 1);
                        w_append(text, number, sizeof(text));
                        w_append(text, "...", sizeof(text));
                        web_progress(text);
                        unsigned int mark = web_mark();
                        WebResource r;
                        if (!web_fetch(url, &r, WEB_RESOURCE_MAX, 0, 0) && r.data) {
                            total += r.length;
                            /* decode, then keep only the pixels */
                            WImage *image = web_png((const unsigned char *)r.data, r.length);
                            if (image) {
                                unsigned int bytes = (unsigned)image->width * (unsigned)image->height * 4;
                                int iw = image->width, ih = image->height;
                                unsigned int *from = image->pixels;
                                web_release(mark);
                                /* the pixels lie above: move them down to the mark */
                                WImage *kept = web_take(sizeof(WImage));
                                unsigned int *pixels = web_take(bytes);
                                if (pixels && pixels != from) for (unsigned int i = 0; i < bytes / 4; i++) pixels[i] = from[i];
                                if (kept && pixels) { kept->width = iw; kept->height = ih; kept->pixels = pixels; n->image = kept; }
                            } else web_release(mark);
                        }
                        count++;
                    }
                }
            }
            if (n->first) { n = n->first; continue; }
            while (n && !n->next) n = n->parent;
            if (n) n = n->next;
        }
    }
    web_prepare_controls(d);
    d->mark = web_mark();
    d->prepared = 1;
}

/* Drops the ::before/::after boxes of the last styling before its memory goes. */
static void strip_pseudo(WDoc *d)
{
    for (WNode *n = d->root; n;) {
        if (n->type == WN_ELEMENT) {
            while (n->first && n->first->pseudo) { n->first = n->first->next; if (n->first) n->first->prev = 0; else n->last = 0; }
            while (n->last && n->last->pseudo) { n->last = n->last->prev; if (n->last) n->last->next = 0; else n->first = 0; }
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
}
void web_unlayout(WDoc *d)
{
    strip_pseudo(d);
    web_release(d->mark);
}
void web_layout(WDoc *d, int width, int viewport_h)
{
    if (!d->prepared) web_load_resources(d, 0);
    strip_pseudo(d);
    web_release(d->mark);
    d->width = width < 100 ? 100 : width;
    d->viewport_h = viewport_h;
    w_doc = d;
    web_style(d);
    web_layout_tree(d);
}

/* ---------------------------------------------------------------- painting */
static int clip_x0, clip_y0, clip_x1, clip_y1;
static inline unsigned int mix(unsigned int dst, unsigned int argb)
{
    unsigned int a = argb >> 24;
    if (a >= 255) return argb & 0xffffff;
    unsigned int k = a + (a >> 7), j = 256 - k;
    return (((argb & 0xff00ff) * k + (dst & 0xff00ff) * j) >> 8 & 0xff00ff) | (((argb & 0x00ff00) * k + (dst & 0x00ff00) * j) >> 8 & 0x00ff00);
}
static void span(WCanvas *c, int x0, int x1, int y, unsigned int argb)
{
    if (y < clip_y0 || y >= clip_y1 || !(argb >> 24)) return;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    unsigned int *line = c->pixels + y * c->width;
    if ((argb >> 24) == 255) for (int x = x0; x < x1; x++) line[x] = argb & 0xffffff;
    else for (int x = x0; x < x1; x++) line[x] = mix(line[x], argb);
}
static int isqrt(int v)
{
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return r;
}
/* How far row dy of a box with corner radius r is pulled in. */
static int inset(int r, int dy, int h)
{
    if (r <= 0) return 0;
    int d = dy < r ? r - dy : dy >= h - r ? dy - (h - r) + 1 : 0;
    if (!d) return 0;
    if (d > r) d = r;
    return r - isqrt(r * r - d * d);
}
static void fill(WCanvas *c, int x, int y, int w, int h, unsigned int argb, int radius)
{
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    int y0 = y < clip_y0 ? clip_y0 : y, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int yy = y0; yy < y1; yy++) { int k = inset(radius, yy - y, h); span(c, x + k, x + w - k, yy, argb); }
}
static unsigned int lerp(unsigned int a, unsigned int b, int t, int total)
{
    if (total <= 0) return a;
    unsigned int out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int ca = (int)(a >> shift & 255), cb = (int)(b >> shift & 255);
        out |= (unsigned)(ca + (cb - ca) * t / total) << shift;
    }
    return out;
}
static void gradient(WCanvas *c, const WItem *it, int x, int y)
{
    int radius = it->radius;
    if (radius > it->w / 2) radius = it->w / 2;
    if (radius > it->h / 2) radius = it->h / 2;
    for (int dy = 0; dy < it->h; dy++) {
        int yy = y + dy;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        int k = inset(radius, dy, it->h);
        if (it->style == 1) span(c, x + k, x + it->w - k, yy, lerp(it->color, it->color2, dy, it->h - 1));
        else for (int dx = k; dx < it->w - k; dx++) { if (x + dx >= clip_x0 && x + dx < clip_x1) { unsigned int *p = c->pixels + yy * c->width + x + dx; *p = mix(*p, lerp(it->color, it->color2, dx, it->w - 1)); } }
    }
}
static unsigned int shade(unsigned int argb, int light)
{
    unsigned int out = argb & 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        int v = (int)(argb >> s & 255);
        v = light ? v + (255 - v) / 2 : v / 2;
        out |= (unsigned)v << s;
    }
    return out;
}
static void border(WCanvas *c, const WItem *it, int x, int y)
{
    int w = it->w, h = it->h;
    const int *bw = it->widths;
    int radius = it->radius;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    int style = it->style;
    if (radius > 0 && bw[0] == bw[1] && bw[1] == bw[2] && bw[2] == bw[3] && it->colors[0] == it->colors[2]) {   /* a rounded outline */
        int t = bw[0];
        for (int dy = 0; dy < h; dy++) {
            int outer = inset(radius, dy, h);
            int inner_r = radius - t;
            int in = dy < t || dy >= h - t ? -1 : t + (inner_r > 0 ? inset(inner_r, dy - t, h - 2 * t) : 0);
            if (in < 0) span(c, x + outer, x + w - outer, y + dy, it->colors[0]);
            else { span(c, x + outer, x + in, y + dy, it->colors[0]); span(c, x + w - in, x + w - outer, y + dy, it->colors[0]); }
        }
        return;
    }
    for (int side = 0; side < 4; side++) {
        int t = bw[side];
        if (t <= 0) continue;
        unsigned int color = it->colors[side];
        if (style == BS_INSET) color = side == 0 || side == 3 ? shade(color, 0) : shade(color, 1);
        else if (style == BS_OUTSET) color = side == 0 || side == 3 ? shade(color, 1) : shade(color, 0);
        else if (style == BS_GROOVE || style == BS_RIDGE) color = shade(color, style == BS_RIDGE ? side == 0 || side == 3 : side == 1 || side == 2);
        int dashed = style == BS_DASHED ? 3 * t : style == BS_DOTTED ? t : 0;
        for (int k = 0; k < t; k++) {
            if (style == BS_DOUBLE && t >= 3 && k >= t / 3 && k < t - t / 3) continue;
            int lx0, lx1, ly;
            if (side == 0 || side == 2) {
                ly = side == 0 ? y + k : y + h - 1 - k;
                lx0 = x + (side == 0 ? k * bw[3] / t : k * bw[3] / t); lx1 = x + w - k * bw[1] / t;
                if (!dashed) span(c, lx0, lx1, ly, color);
                else for (int sx = lx0; sx < lx1; sx += 2 * dashed) span(c, sx, sx + dashed < lx1 ? sx + dashed : lx1, ly, color);
            } else {
                int cx = side == 3 ? x + k : x + w - 1 - k;
                int top = y + k * bw[0] / t, bottom = y + h - k * bw[2] / t;
                for (int yy = top; yy < bottom; yy++) {
                    if (dashed && ((yy - top) / dashed) & 1) continue;
                    span(c, cx, cx + 1, yy, color);
                }
            }
        }
    }
}
static void image(WCanvas *c, const WItem *it, int x, int y)
{
    const WImage *im = it->image;
    if (!im || it->w <= 0 || it->h <= 0) return;
    int y0 = y < clip_y0 ? clip_y0 : y, y1 = y + it->h > clip_y1 ? clip_y1 : y + it->h;
    int x0 = x < clip_x0 ? clip_x0 : x, x1 = x + it->w > clip_x1 ? clip_x1 : x + it->w;
    int faded = it->style;
    for (int yy = y0; yy < y1; yy++) {
        int sy = (yy - y) * im->height / it->h;
        const unsigned int *row = im->pixels + sy * im->width;
        unsigned int *line = c->pixels + yy * c->width;
        for (int xx = x0; xx < x1; xx++) {
            unsigned int p = row[(xx - x) * im->width / it->w];
            if (faded) p = ((p >> 24) / 2) << 24 | (p & 0xffffff);
            if ((p >> 24) == 255) line[xx] = p & 0xffffff;
            else if (p >> 24) line[xx] = mix(line[xx], p);
        }
    }
}
static void text(WCanvas *c, int x, int baseline, int size, int family, int bold, const char *s, int length, unsigned int color)
{
    WFont f = {(unsigned char)family, (unsigned char)bold, 0, size};
    wf_draw(c, x, baseline, &f, s, length, color, 0, clip_x0, clip_y0, clip_x1, clip_y1);
}
static int text_px(int size, int family, const char *s, int length)
{
    WFont f = {(unsigned char)family, 0, 0, size};
    return (wf_text_width(&f, s, length, 0) + 15) / 16;
}
static void disc(WCanvas *c, int cx16, int cy16, int r16, int t16, unsigned int argb);
static void control(WCanvas *c, WDoc *d, const WItem *it, int x, int y)
{
    WNode *n = it->node;
    WControl *k = n->control;
    int size = it->size, family = it->family;
    WFont f = {(unsigned char)family, 0, 0, size};
    int baseline = y + (it->h + wf_ascent(&f) - wf_descent(&f)) / 2;
    int sx0 = clip_x0, sy0 = clip_y0, sx1 = clip_x1, sy1 = clip_y1;
    if (x > clip_x0) clip_x0 = x;
    if (y > clip_y0) clip_y0 = y;
    if (x + it->w < clip_x1) clip_x1 = x + it->w;
    if (y + it->h < clip_y1) clip_y1 = y + it->h;
    int focused = k && d->focused >= 0 && d->focused < d->control_count && d->controls[d->focused] == k;
    unsigned int ink = it->color | 0xff000000u;
    if (!k) {
        if (n->tag == T_IMG) {   /* a missing picture: its alt text */
            if (it->text) text(c, x + 1, baseline, size, family, 0, it->text, it->length, 0xff707070u);
        } else if (n->tag != T_SVG && n->tag != T_MATH) {
            fill(c, x, y, it->w, it->h, 0xffe8e8e8u, 0);
            const char *label = n->tag == T_IFRAME ? "frame" : n->tag == T_VIDEO ? "video" : n->tag == T_AUDIO ? "audio" : "plug-in";
            int tw = text_px(13, 0, label, w_len(label));
            text(c, x + (it->w - tw) / 2, y + it->h / 2 + 4, 13, 0, 0, label, w_len(label), 0xff808080u);
        }
    } else switch (k->type) {
    case WC_CHECKBOX: case WC_RADIO: {
        int s = it->w < it->h ? it->w : it->h;
        if (k->type == WC_RADIO) {
            int r16 = s * 8, cx16 = x * 16 + r16, cy16 = y * 16 + r16;
            disc(c, cx16, cy16, r16, 0, 0xff767676u);
            disc(c, cx16, cy16, r16 - 16, 0, k->checked ? 0xff0b57d0u : 0xffffffffu);
            if (k->checked) disc(c, cx16, cy16, r16 * 2 / 5, 0, 0xffffffffu);
            break;
        }
        fill(c, x, y, s, s, 0xff767676u, 2);
        fill(c, x + 1, y + 1, s - 2, s - 2, k->checked ? 0xff0b57d0u : 0xffffffffu, 1);
        if (k->checked && k->type == WC_CHECKBOX)
            for (int i = 0; i < s - 6; i++) {   /* a tick */
                int px0 = x + 3 + i, py0 = i < (s - 6) / 3 ? y + s / 2 + i - 1 : y + s / 2 + (s - 6) / 3 - (i - (s - 6) / 3) - 1;
                span(c, px0, px0 + 2, py0, 0xffffffffu); span(c, px0, px0 + 2, py0 + 1, 0xffffffffu);
            }
        break;
    }
    case WC_SUBMIT: case WC_RESET: case WC_BUTTON: case WC_FILE: {
        const char *label = k->value[0] ? k->value : k->type == WC_RESET ? "Reset" : k->type == WC_FILE ? "Browse..." : k->type == WC_SUBMIT ? "Submit" : "";
        int tw = text_px(size, family, label, w_len(label));
        text(c, x + (it->w - tw) / 2, baseline, size, family, 0, label, w_len(label), ink);
        break;
    }
    case WC_SELECT: {
        char option[256];
        int index = 0;
        option[0] = 0;
        for (WNode *o = n->first; o;) {
            if (o->tag == T_OPTION) { if (index == k->selected) { web_text_of(o, option, sizeof(option)); break; } index++; }
            if (o->tag == T_OPTGROUP && o->first) { o = o->first; continue; }
            if (!o->next && o->parent != n) o = o->parent;
            o = o->next;
        }
        text(c, x, baseline, size, family, 0, option, w_len(option), ink);
        clip_x1 = sx1;
        int ax = x + it->w + 6, ay = y + it->h / 2 - 2;
        for (int i = 0; i < 4; i++) span(c, ax + i, ax + 8 - i, ay + i, 0xff404040u);
        break;
    }
    case WC_TEXTAREA: {
        int lh = size * 6 / 5 + 1, line_y = y + wf_ascent(&f);
        const char *p = k->value;
        while (*p && line_y - wf_ascent(&f) < y + it->h) {
            const char *e = p;
            while (*e && *e != '\n') e++;
            text(c, x, line_y, size, 2, 0, p, (int)(e - p), ink);
            line_y += lh;
            p = *e ? e + 1 : e;
        }
        if (!k->value[0] && w_attr(n, "placeholder")) text(c, x, y + wf_ascent(&f), size, family, 0, w_attr(n, "placeholder"), w_len(w_attr(n, "placeholder")), 0xff909090u);
        if (focused) {
            int lines = 0; const char *last = k->value;
            for (int i = 0; i < k->cursor && k->value[i]; i++) if (k->value[i] == '\n') { lines++; last = k->value + i + 1; }
            WFont m = {2, 0, 0, size};
            int cx = x + (wf_text_width(&m, last, (int)(k->value + k->cursor - last), 0) + 15) / 16;
            fill(c, cx, y + lines * lh + 1, 1, lh - 2, 0xff000000u, 0);
        }
        break;
    }
    case WC_HIDDEN: case WC_IMAGE: break;
    default: {
        const char *value = k->value;
        char dots[512];
        int length = w_len(value);
        if (k->type == WC_PASSWORD) {
            int n2 = 0;
            for (int i = 0; i < length && n2 < 500; i++) if (((unsigned char)value[i] & 0xc0) != 0x80) n2 += w_utf8_put(dots + n2, 0x2022);
            dots[n2] = 0; value = dots; length = n2;
        }
        /* keep the caret in view */
        int caret_bytes = k->type == WC_PASSWORD ? 0 : k->cursor;
        if (k->type == WC_PASSWORD) { for (int i = 0, chars = 0; i < k->cursor; i++) if (((unsigned char)k->value[i] & 0xc0) != 0x80) chars++, caret_bytes = chars * 3; }
        int caret_x = (wf_text_width(&f, value, caret_bytes > length ? length : caret_bytes, 0) + 15) / 16;
        int scroll = caret_x > it->w - 2 ? caret_x - it->w + 2 : 0;
        if (length) text(c, x - scroll, baseline, size, family, 0, value, length, ink);
        else if (w_attr(n, "placeholder")) text(c, x, baseline, size, family, 0, w_attr(n, "placeholder"), w_len(w_attr(n, "placeholder")), 0xff909090u);
        if (focused) fill(c, x + caret_x - scroll, baseline - wf_ascent(&f), 1, wf_ascent(&f) + wf_descent(&f), 0xff000000u, 0);
        break;
    }
    }
    clip_x0 = sx0; clip_y0 = sy0; clip_x1 = sx1; clip_y1 = sy1;
}
/* An antialiased disc (or ring of width t when t > 0), center and radius in 1/16 px. */
static void disc(WCanvas *c, int cx16, int cy16, int r16, int t16, unsigned int argb)
{
    int x0 = (cx16 - r16) / 16 - 1, x1 = (cx16 + r16) / 16 + 2, y0 = (cy16 - r16) / 16 - 1, y1 = (cy16 + r16) / 16 + 2;
    unsigned int alpha = argb >> 24;
    for (int y = y0; y < y1; y++) {
        if (y < clip_y0 || y >= clip_y1) continue;
        for (int x = x0; x < x1; x++) {
            if (x < clip_x0 || x >= clip_x1) continue;
            int cover = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int dx = x * 16 + sx * 4 + 2 - cx16, dy = y * 16 + sy * 4 + 2 - cy16, d2 = dx * dx + dy * dy;
                    if (d2 <= r16 * r16 && (t16 <= 0 || d2 >= (r16 - t16) * (r16 - t16))) cover++;
                }
            if (!cover) continue;
            unsigned int *p = c->pixels + y * c->width + x;
            *p = mix(*p, (alpha * (unsigned)cover / 16) << 24 | (argb & 0xffffff));
        }
    }
}
static void marker(WCanvas *c, const WItem *it, int x, int y)
{
    if (it->text) { text(c, x, y, it->size, it->family, 0, it->text, it->length, it->color); return; }
    int r16 = it->size * 16 * 17 / 100;   /* about a third of the font size across */
    if (r16 < 32) r16 = 32;
    int cx16 = (x + it->w) * 16 - r16 - 40, cy16 = y * 16 - it->size * 16 * 32 / 100;
    if (it->style == LS_SQUARE) { int s = r16 * 2 / 16; fill(c, (cx16 - r16) / 16, (cy16 - r16) / 16, s, s, it->color, 0); }
    else disc(c, cx16, cy16, r16, it->style == LS_CIRCLE ? 20 : 0, it->color);
}
void web_paint(WDoc *d, WCanvas *c, int x, int y, int w, int h, int scroll)
{
    int stack[64][4], depth = 0;
    clip_x0 = x < 0 ? 0 : x; clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > c->width ? c->width : x + w; clip_y1 = y + h > c->height ? c->height : y + h;
    fill(c, x, y, w, h, d->canvas_color | 0xff000000u, 0);
    for (int i = 0; i < d->item_count; i++) {
        WItem *it = &d->items[i];
        int ix = it->x + x, iy = it->y + y - (it->fixed ? 0 : scroll);
        if (it->kind == WI_CLIP) {
            if (depth < 64) { stack[depth][0] = clip_x0; stack[depth][1] = clip_y0; stack[depth][2] = clip_x1; stack[depth][3] = clip_y1; }
            depth++;
            if (ix > clip_x0) clip_x0 = ix;
            if (iy > clip_y0) clip_y0 = iy;
            if (ix + it->w < clip_x1) clip_x1 = ix + it->w;
            if (iy + it->h < clip_y1) clip_y1 = iy + it->h;
            continue;
        }
        if (it->kind == WI_UNCLIP) {
            if (depth > 0) { depth--; if (depth < 64) { clip_x0 = stack[depth][0]; clip_y0 = stack[depth][1]; clip_x1 = stack[depth][2]; clip_y1 = stack[depth][3]; } }
            continue;
        }
        if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) continue;
        int top = it->kind == WI_TEXT || it->kind == WI_MARKER ? iy - it->size * 2 : iy, bottom = it->kind == WI_TEXT || it->kind == WI_MARKER ? iy + it->size : iy + it->h;
        if (bottom < clip_y0 || top >= clip_y1) continue;
        switch (it->kind) {
        case WI_RECT: if (it->style) gradient(c, it, ix, iy); else fill(c, ix, iy, it->w, it->h, it->color, it->radius); break;
        case WI_BORDER: border(c, it, ix, iy); break;
        case WI_TEXT: {
            WFont f = {it->family, it->bold, it->italic, it->size};
            if (i == d->find_item) fill(c, ix, iy - wf_ascent(&f), it->w, wf_ascent(&f) + wf_descent(&f), 0xffffe066u, 0);
            wf_draw(c, ix, iy, &f, it->text, it->length, it->color, it->letter_spacing, clip_x0, clip_y0, clip_x1, clip_y1);
            break;
        }
        case WI_LINE: fill(c, ix, iy, it->w, it->h, it->color, 0); break;
        case WI_IMAGE: image(c, it, ix, iy); break;
        case WI_CONTROL: control(c, d, it, ix, iy); break;
        case WI_MARKER: marker(c, it, ix, iy); break;
        }
    }
}

/* ---------------------------------------------------------------- hit testing */
WItem *web_hit(WDoc *d, int x, int y, int scroll)
{
    WItem *found = 0;
    for (int i = d->item_count - 1; i >= 0; i--) {
        WItem *it = &d->items[i];
        if (it->kind == WI_NONE || it->kind == WI_CLIP || it->kind == WI_UNCLIP) continue;
        int iy = it->y - (it->fixed ? 0 : scroll), top = iy, bottom = iy + it->h;
        if (it->kind == WI_TEXT) { top = iy - it->size; bottom = iy + it->size / 3 + 1; }
        if (x < it->x || x >= it->x + it->w || y < top || y >= bottom) continue;
        if (it->link || it->kind == WI_CONTROL) return it;
        if (!found) found = it;
        /* inside a button: the button */
        for (WNode *n = it->node; n; n = n->parent) if (n->control) return it;
    }
    return found;
}
int web_anchor_y(WDoc *d, const char *fragment)
{
    if (!fragment || !fragment[0]) return -1;
    char decoded[256]; int k = 0;
    for (const char *p = fragment; *p && k < 255; p++) {
        if (*p == '%' && p[1] && p[2]) {
            int a = p[1], b = p[2];
            int va = a >= '0' && a <= '9' ? a - '0' : (a | 32) - 'a' + 10, vb = b >= '0' && b <= '9' ? b - '0' : (b | 32) - 'a' + 10;
            decoded[k++] = (char)(va * 16 + vb); p += 2;
        } else decoded[k++] = *p;
    }
    decoded[k] = 0;
    for (WNode *n = d->root; n;) {
        if (n->type == WN_ELEMENT && ((n->id && w_eq(n->id, decoded)) || (n->tag == T_A && w_attr(n, "name") && w_eq(w_attr(n, "name"), decoded))))
            return n->y;
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    return -1;
}
/* A case-insensitive search through the words of the page. */
static int lower_code(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0x410 && c <= 0x42f) return c + 32;
    if (c >= 0x400 && c <= 0x40f) return c + 80;
    return c;
}
int web_find(WDoc *d, const char *query, int from)
{
    int ql = w_len(query);
    if (!ql) return -1;
    for (int pass = 0; pass < 2; pass++)
        for (int i = pass ? 0 : from + 1; i < (pass ? from + 1 : d->item_count); i++) {
            if (i < 0 || d->items[i].kind != WI_TEXT) continue;
            /* match from some position in item i, continuing into the next words */
            const WItem *it = &d->items[i];
            for (int start = 0; start < it->length; start++) {
                const unsigned char *q = (const unsigned char *)query, *qe = q + ql;
                int item = i, pos = start, ok = 1;
                while (q < qe) {
                    if (*q == ' ') {
                        q++;
                        if (pos < d->items[item].length) { if (d->items[item].text[pos] == ' ') pos++; else { ok = 0; break; } continue; }
                        int next = item + 1;
                        while (next < d->item_count && d->items[next].kind != WI_TEXT) next++;
                        if (next >= d->item_count) { ok = 0; break; }
                        item = next; pos = 0;
                        continue;
                    }
                    if (pos >= d->items[item].length) { ok = 0; break; }
                    const unsigned char *p = (const unsigned char *)d->items[item].text + pos, *pe = (const unsigned char *)d->items[item].text + d->items[item].length;
                    int a = w_utf8_decode(&p, pe), b = w_utf8_decode(&q, qe);
                    if (lower_code(a) != lower_code(b)) { ok = 0; break; }
                    pos = (int)(p - (const unsigned char *)d->items[item].text);
                }
                if (ok) return i;
                while (start + 1 < it->length && ((unsigned char)it->text[start + 1] & 0xc0) == 0x80) start++;
            }
        }
    return -1;
}

/* ---------------------------------------------------------------- forms */
static void add_pair(char *out, int capacity, const char *name, const char *value)
{
    char encoded[1024];
    if (out[0]) w_append(out, "&", capacity);
    w_url_encode(name, encoded, sizeof(encoded));
    w_append(out, encoded, capacity);
    w_append(out, "=", capacity);
    w_url_encode(value, encoded, sizeof(encoded));
    w_append(out, encoded, capacity);
}
int web_submit(WDoc *d, WControl *submitter, char *url, int capacity, char *body, int body_capacity)
{
    WNode *form = submitter ? submitter->form : 0;
    if (!form) return -1;
    char query[4096] = "";
    for (int i = 0; i < d->control_count; i++) {
        WControl *k = d->controls[i];
        if (k->form != form) continue;
        const char *name = w_attr(k->node, "name");
        if (!name || !name[0] || w_attr(k->node, "disabled")) continue;
        switch (k->type) {
        case WC_SUBMIT: case WC_IMAGE:
            if (k != submitter) break;
            if (k->type == WC_IMAGE) { char n2[256]; w_copy(n2, name, sizeof(n2)); w_append(n2, ".x", sizeof(n2)); add_pair(query, sizeof(query), n2, "1"); w_copy(n2, name, sizeof(n2)); w_append(n2, ".y", sizeof(n2)); add_pair(query, sizeof(query), n2, "1"); }
            else add_pair(query, sizeof(query), name, k->value);
            break;
        case WC_RESET: case WC_BUTTON: case WC_FILE: break;
        case WC_CHECKBOX: case WC_RADIO: if (k->checked) add_pair(query, sizeof(query), name, k->value[0] ? k->value : "on"); break;
        case WC_SELECT: {
            int index = 0;
            for (WNode *o = k->node->first; o;) {
                if (o->tag == T_OPTION) {
                    if (index == k->selected) {
                        char text[256];
                        const char *v = w_attr(o, "value");
                        add_pair(query, sizeof(query), name, v ? v : web_text_of(o, text, sizeof(text)));
                        break;
                    }
                    index++;
                }
                if (o->tag == T_OPTGROUP && o->first) { o = o->first; continue; }
                if (!o->next && o->parent != k->node) o = o->parent;
                o = o->next;
            }
            break;
        }
        default: add_pair(query, sizeof(query), name, k->value); break;
        }
    }
    const char *action = w_attr(form, "action");
    if (submitter && w_attr(submitter->node, "formaction")) action = w_attr(submitter->node, "formaction");
    char target[1024];
    w_resolve_url(d->base, action && action[0] ? action : d->url, target, sizeof(target));
    const char *method = w_attr(form, "method");
    if (method && w_eq_ci(method, "post")) {
        w_copy(url, target, capacity);
        w_copy(body, query, body_capacity);
        return 1;
    }
    for (char *p = target; *p; p++) if (*p == '?' || *p == '#') { *p = 0; break; }
    w_copy(url, target, capacity);
    w_append(url, "?", capacity);
    w_append(url, query, capacity);
    return 0;
}
