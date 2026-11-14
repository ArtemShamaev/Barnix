/* Gouse Web: layout (CSS 2.2 visual formatting, Flexbox, a Grid subset,
 * tables) into a display list. Everything is in document pixels; boxes
 * are laid out once, top to bottom, and moved or stretched afterwards
 * by shifting their display items. */
#include "web.h"

static WDoc *doc;
static int viewport_w, viewport_h;
static WNode *current_link;
static int depth_now;
#define MAX_DEPTH 160

/* ---------------------------------------------------------------- display items */
WItem *w_item(WDoc *d, int kind)
{
    if (d->item_count == d->item_capacity) {
        int capacity = d->item_capacity ? d->item_capacity * 2 : 8192;
        WItem *bigger = web_alloc_big(sizeof(WItem) * (unsigned)capacity);
        if (!bigger) { static WItem junk; junk.kind = WI_NONE; return &junk; }
        for (int i = 0; i < d->item_count; i++) bigger[i] = d->items[i];
        d->items = bigger; d->item_capacity = capacity;
    }
    WItem *it = &d->items[d->item_count++];
    unsigned int *p = (unsigned int *)it;
    for (unsigned int i = 0; i < sizeof(WItem) / 4; i++) p[i] = 0;
    it->kind = (unsigned char)kind;
    it->link = current_link;
    return it;
}
static void shift_items(int first, int last, int dx, int dy)
{
    for (int i = first; i < last; i++) { doc->items[i].x += dx; doc->items[i].y += dy; }
}
/* Moves the boxes of a subtree with its items. */
static void shift_subtree(WNode *n, int dx, int dy)
{
    WNode *stop = n;
    while (n) {
        if (n->type == WN_ELEMENT) { n->x += dx; n->y += dy; n->static_x += dx; n->static_y += dy; }
        if (n->first) { n = n->first; continue; }
        while (n && n != stop && !n->next) n = n->parent;
        if (!n || n == stop) break;
        n = n->next;
    }
}
static void shift_nodes(WNode *n, int dx, int dy)
{
    if (!dx && !dy) return;
    if (n->pseudo != 3) { shift_subtree(n, dx, dy); return; }
    n->x += dx; n->y += dy;
    for (WNode *c = n->first; c && c != n->anon_stop; c = c->next) shift_subtree(c, dx, dy);
}
static void shift_box(WNode *n, int first, int last, int dx, int dy)
{
    shift_items(first, last, dx, dy);
    shift_nodes(n, dx, dy);
}

/* ---------------------------------------------------------------- style helpers */
static int line_height(const WStyle *s)
{
    if (s->line_height > 0) return s->line_height;
    if (s->line_height < 0) return s->font.size * -s->line_height / 100;
    return (wf_ascent(&s->font) + wf_descent(&s->font)) * 23 / 20 + 1;
}
static int number_of(const char *s)
{
    int v = 0, negative = *s == '-';
    if (negative) s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    return negative ? -v : v;
}
static int px(const WLen *l, int containing) { return l->kind == WL_LEN || l->kind >= WL_MIN ? w_resolve(l, containing, 0) : 0; }
static int is_auto(const WLen *l) { return l->kind == WL_AUTO || l->kind == WL_CONTENT; }
typedef struct { int t, r, b, l; } Edges;
static void edges(const WStyle *s, int cw, Edges *m, Edges *b, Edges *p)
{
    if (m) { m->t = px(&s->margin[0], cw); m->r = px(&s->margin[1], cw); m->b = px(&s->margin[2], cw); m->l = px(&s->margin[3], cw); }
    if (b) { b->t = s->border_width[0]; b->r = s->border_width[1]; b->b = s->border_width[2]; b->l = s->border_width[3]; }
    if (p) {
        p->t = px(&s->padding[0], cw); p->r = px(&s->padding[1], cw); p->b = px(&s->padding[2], cw); p->l = px(&s->padding[3], cw);
        if (p->t < 0) p->t = 0;
        if (p->r < 0) p->r = 0;
        if (p->b < 0) p->b = 0;
        if (p->l < 0) p->l = 0;
    }
}
static int replaced(const WNode *n)
{
    switch (n->tag) {
    case T_IMG: case T_INPUT: case T_SELECT: case T_TEXTAREA: case T_IFRAME: case T_VIDEO: case T_CANVAS:
    case T_OBJECT: case T_EMBED: case T_SVG: case T_MATH: case T_AUDIO:
        return 1;
    }
    return 0;
}
static int inline_level(const WNode *n)
{
    if (n->type == WN_TEXT) return 1;
    const WStyle *s = n->style;
    if (!s) return 0;
    if (s->position == P_ABSOLUTE || s->position == P_FIXED) return 0;
    if (s->floating) return 0;
    return s->display == D_INLINE || s->display == D_INLINE_BLOCK || s->display == D_INLINE_FLEX || s->display == D_INLINE_TABLE ||
           s->display == D_INLINE_GRID || s->display == D_CONTENTS || n->tag == T_BR || n->tag == T_WBR;
}
static int blank_text(const WNode *n)
{
    if (n->type != WN_TEXT) return 0;
    if (n->style && (n->style->white_space == WS_PRE || n->style->white_space == WS_PRE_WRAP)) return n->length == 0;
    for (int i = 0; i < n->length; i++) if (!w_space((unsigned char)n->text[i])) return 0;
    return 1;
}
static int out_of_flow(const WNode *n) { return n->type == WN_ELEMENT && n->style && (n->style->position == P_ABSOLUTE || n->style->position == P_FIXED); }
static int hidden(const WNode *n) { return n->type == WN_ELEMENT && (!n->style || n->style->display == D_NONE || n->style->display == D_TABLE_COLUMN); }
static WStyle *anonymous_style(const WStyle *parent)
{
    WStyle *s = web_alloc(sizeof(WStyle));
    *s = *parent;
    WLen autolen = {WL_AUTO, 0, 0, 0}, zero = {WL_LEN, 0, 0, 0}, none = {WL_NONE, 0, 0, 0};
    s->display = D_BLOCK; s->position = P_STATIC; s->floating = 0; s->clear = 0; s->overflow = 0;
    s->background = 0; s->has_gradient = 0; s->radius = 0; s->opacity = 255;
    s->width = s->height = s->min_width = s->min_height = s->flex_basis = autolen;
    s->max_width = s->max_height = none;
    for (int i = 0; i < 4; i++) { s->margin[i] = zero; s->padding[i] = zero; s->offset[i] = autolen; s->border_width[i] = 0; s->border_style[i] = 0; }
    s->flex_grow = 0; s->flex_shrink = 100; s->order = 0; s->align_self = 255; s->column_span = 1; s->column_start = 0;
    s->content = 0; s->column_count = 0; s->columns = 0; s->masked = 0;
    return s;
}
static void defer(WNode *n, int x, int y);
/* Flex and grid containers make an anonymous block of each run of text and
 * inline children; it only points at them, the document stays as it is. */
static int flex_child(const WNode *c) { return c->type == WN_ELEMENT && !(c->style && c->style->display == D_INLINE && !c->style->floating); }
static WNode *anonymous(WNode *parent, WNode *first, WNode *stop)
{
    WNode *a = web_alloc(sizeof(WNode));
    a->type = WN_ELEMENT; a->pseudo = 3; a->name = "::anonymous"; a->parent = parent;
    a->style = anonymous_style(parent->style);
    a->first = first; a->anon_stop = stop;
    return a;
}
/* The children of a flex or grid container as boxes: elements, and text
 * runs in anonymous blocks. Returns the count; out gets up to max. */
static int box_children(WNode *n, WNode **out, int max, int cx, int cy)
{
    int count = 0;
    for (WNode *c = n->first; c;) {
        if (c->type == WN_ELEMENT && (hidden(c))) { c = c->next; continue; }
        if (c->type == WN_ELEMENT && out_of_flow(c)) { defer(c, cx, cy); c = c->next; continue; }
        if (flex_child(c) || blank_text(c)) {
            if (flex_child(c) && count < max) out[count++] = c;
            c = c->next;
            continue;
        }
        WNode *start = c;
        int content = 0;
        while (c && !flex_child(c)) { if (!blank_text(c) && !(c->type == WN_ELEMENT && (hidden(c) || out_of_flow(c)))) content = 1; c = c->next; }
        if (content && count < max) out[count++] = anonymous(n, start, c);
    }
    return count;
}

/* ---------------------------------------------------------------- floats */
typedef struct { int x, y, w, h, side; } Float;
static Float floats[512];
static int float_count, float_base;
static void float_space(int y, int h, int x0, int x1, int *left, int *right)
{
    *left = x0; *right = x1;
    for (int i = float_base; i < float_count; i++) {
        Float *f = &floats[i];
        if (y + h <= f->y || y >= f->y + f->h) continue;
        if (f->side == 1 && f->x + f->w > *left && f->x < x1) *left = f->x + f->w;
        if (f->side == 2 && f->x < *right && f->x + f->w > x0) *right = f->x;
    }
}
static int float_bottom(int side_mask)
{
    int b = -1000000;
    for (int i = float_base; i < float_count; i++) if (floats[i].side & side_mask) { int e = floats[i].y + floats[i].h; if (e > b) b = e; }
    return b;
}
/* The next y below y where some float ends (to retry a line that did not fit). */
static int next_float_end(int y)
{
    int best = 1000000000;
    for (int i = float_base; i < float_count; i++) { int e = floats[i].y + floats[i].h; if (e > y && e < best) best = e; }
    return best == 1000000000 ? -1 : best;
}

/* ---------------------------------------------------------------- deferred boxes */
typedef struct { WNode *n; } Deferred;
static Deferred deferred[512];
static int deferred_count;
static void defer(WNode *n, int x, int y)
{
    n->static_x = x; n->static_y = y;
    if (deferred_count < 512) deferred[deferred_count++].n = n;
}

/* ---------------------------------------------------------------- intrinsic widths */
static int layout_box(WNode *n, int x, int y, int avail, int forced_w, int forced_h, int containing_h);
static void text_extent(const WNode *t, int *min, int *max)
{
    const WStyle *s = t->style;
    int ws = s->white_space;
    int nowrap = ws == WS_PRE || ws == WS_NOWRAP;
    int word = 0, line = 0, space = wf_advance(&s->font, ' ') + s->word_spacing * 16;
    *min = *max = 0;
    const unsigned char *p = (const unsigned char *)t->text, *end = p + t->length;
    int pending = 0;
    while (p < end) {
        const unsigned char *at = p;
        int c = w_utf8_decode(&p, end);
        if (c == '\n' && (ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE)) {
            if (line > *max) *max = line;
            if (word > *min) *min = word;
            line = word = 0; pending = 0;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (word > *min && !nowrap) *min = word;
            word = 0;
            if (ws == WS_PRE || ws == WS_PRE_WRAP) line += space;
            else pending = line > 0;
            continue;
        }
        (void)at;
        if (pending) { line += space; pending = 0; if (nowrap) word += space; }
        int a = wf_advance(&s->font, (unsigned)c) + s->letter_spacing * 16;
        word += a; line += a;
    }
    if (word > *min) *min = word;
    if (line > *max) *max = line;
    if (nowrap) *min = *max;
    *min = (*min + 15) / 16; *max = (*max + 15) / 16;
}
static void replaced_size(WNode *n, int cw, int *w, int *h);
static void intrinsic(WNode *n, int *min, int *max)
{
    *min = *max = 0;
    if (n->type == WN_TEXT) { text_extent(n, min, max); return; }
    const WStyle *s = n->style;
    if (!s || s->display == D_NONE) return;
    if (n->imark == doc->layout_pass) { *min = n->imin; *max = n->imax; return; }
    if (depth_now > MAX_DEPTH) return;
    depth_now++;
    Edges m, b, p;
    edges(s, 0, &m, &b, &p);
    int extra = b.l + b.r + p.l + p.r;
    int lo = 0, hi = 0;
    if (s->width.kind == WL_LEN && !s->width.pct) {
        lo = hi = s->width.px + (s->box_sizing ? 0 : extra);
    } else if (replaced(n)) {
        int w, h;
        replaced_size(n, 0, &w, &h);
        lo = hi = w + extra;
        if (s->width.kind == WL_LEN && s->width.pct) lo = extra;
    } else {
        int run_min = 0, run_max = 0, line = 0;
        int flex_row = (s->display == D_FLEX || s->display == D_INLINE_FLEX) && s->flex_direction < 2;
        int grid = s->display == D_GRID || s->display == D_INLINE_GRID;
        int table = s->display == D_TABLE || s->display == D_INLINE_TABLE;
        int row = s->display == D_TABLE_ROW;
        int children = 0;
        for (WNode *c = n->first; c && c != n->anon_stop; c = c->next) {
            if (hidden(c) || out_of_flow(c)) continue;
            int cmin, cmax;
            if (c->type == WN_ELEMENT && (c->tag == T_TBODY || c->tag == T_THEAD || c->tag == T_TFOOT || c->style->display == D_TABLE_ROW_GROUP) && table) {
                intrinsic(c, &cmin, &cmax);   /* row groups: the widest row */
                if (cmin > run_min) run_min = cmin;
                if (cmax > run_max) run_max = cmax;
                continue;
            }
            intrinsic(c, &cmin, &cmax);
            if (table && c->type == WN_ELEMENT && c->style->display == D_TABLE_CAPTION) {   /* captions follow the table's width */
                if (cmin > run_min) run_min = cmin;
                if (cmin > run_max) run_max = cmin;
                continue;
            }
            if (flex_row || row) {
                run_max += cmax + (children ? s->gap_column : 0);
                if (s->flex_wrap) { if (cmin > run_min) run_min = cmin; }
                else run_min += cmin;
                children++;
                continue;
            }
            if (grid) {
                if (cmax > run_max) run_max = cmax;
                if (cmin > run_min) run_min = cmin;
                children++;
                continue;
            }
            if (c->tag == T_BR) { if (line > run_max) run_max = line; line = 0; continue; }
            if (inline_level(c)) {
                line += cmax;
                if (cmin > run_min) run_min = cmin;
            } else {
                if (line > run_max) run_max = line;
                line = 0;
                if (cmax > run_max) run_max = cmax;
                if (cmin > run_min) run_min = cmin;
            }
        }
        if (line > run_max) run_max = line;
        if (grid) {
            int columns = s->column_count > 0 ? s->column_count : 1;
            if (s->column_auto_fill) columns = children > 4 ? 4 : children ? children : 1;
            run_max = run_max * columns + s->gap_column * (columns - 1);
        }
        if (s->display == D_LIST_ITEM && !s->list_inside) { }
        lo = run_min + extra; hi = run_max + extra;
        if (s->min_width.kind == WL_LEN && !s->min_width.pct) { int mw = s->min_width.px + (s->box_sizing ? 0 : extra); if (lo < mw) lo = mw; if (hi < mw) hi = mw; }
    }
    if (s->max_width.kind == WL_LEN && !s->max_width.pct) {
        int mw = s->max_width.px + (s->box_sizing ? 0 : extra);
        if (hi > mw) hi = mw;
        if (lo > mw) lo = mw;
    }
    if (lo > hi) hi = lo;
    if (m.l > 0) { lo += m.l; hi += m.l; }
    if (m.r > 0) { lo += m.r; hi += m.r; }
    n->imark = doc->layout_pass; n->imin = lo; n->imax = hi;
    *min = lo; *max = hi;
    depth_now--;
}

/* ---------------------------------------------------------------- replaced elements */
static WControl *control_of(WNode *n)
{
    if (n->control) return n->control;
    WControl *c = web_alloc(sizeof(WControl));
    c->node = n;
    for (WNode *f = n->parent; f; f = f->parent) if (f->tag == T_FORM) { c->form = f; break; }
    const char *type = w_attr(n, "type");
    if (n->tag == T_SELECT) {
        c->type = WC_SELECT;
        int index = 0;
        c->selected = 0;
        for (WNode *o = n->first; o;) {
            if (o->tag == T_OPTION) { if (w_attr(o, "selected")) c->selected = index; index++; }
            if (o->tag == T_OPTGROUP && o->first) { o = o->first; continue; }
            if (!o->next && o->parent != n && o->parent->tag == T_OPTGROUP) { o = o->parent; }
            o = o->next;
        }
        c->option_count = index;
    } else if (n->tag == T_TEXTAREA) {
        c->type = WC_TEXTAREA;
        c->capacity = 8192;
        c->value = web_alloc((unsigned)c->capacity);
        char text[8192];
        web_text_of(n, text, sizeof(text));
        const char *t = text;
        if (*t == '\n') t++;
        w_copy(c->value, t, c->capacity);
    } else if (n->tag == T_BUTTON) {
        c->type = !type || w_eq_ci(type, "submit") ? WC_SUBMIT : w_eq_ci(type, "reset") ? WC_RESET : WC_BUTTON;
    } else {
        c->type = !type ? WC_TEXT : w_eq_ci(type, "password") ? WC_PASSWORD : w_eq_ci(type, "checkbox") ? WC_CHECKBOX :
                  w_eq_ci(type, "radio") ? WC_RADIO : w_eq_ci(type, "submit") ? WC_SUBMIT : w_eq_ci(type, "reset") ? WC_RESET :
                  w_eq_ci(type, "button") ? WC_BUTTON : w_eq_ci(type, "hidden") ? WC_HIDDEN : w_eq_ci(type, "image") ? WC_IMAGE :
                  w_eq_ci(type, "file") ? WC_FILE : WC_TEXT;
        c->checked = w_attr(n, "checked") != 0;
    }
    if (c->type != WC_TEXTAREA) {
        c->capacity = 512;
        c->value = web_alloc((unsigned)c->capacity);
        const char *v = w_attr(n, "value");
        if (v) w_copy(c->value, v, c->capacity);
        c->cursor = w_len(c->value);
    }
    n->control = c;
    if (doc->control_count % 64 == 0) {
        WControl **bigger = web_alloc(sizeof(WControl *) * (unsigned)(doc->control_count + 64));
        for (int i = 0; i < doc->control_count; i++) bigger[i] = doc->controls[i];
        doc->controls = bigger;
    }
    doc->controls[doc->control_count++] = c;
    return c;
}
static const char *option_text(WNode *select, int index, char *out, int capacity)
{
    int k = 0;
    out[0] = 0;
    for (WNode *o = select->first; o;) {
        if (o->tag == T_OPTION) {
            if (k == index) {
                web_text_of(o, out, capacity);
                if (!out[0] && w_attr(o, "label")) w_copy(out, w_attr(o, "label"), capacity);
                return out;
            }
            k++;
        }
        if (o->tag == T_OPTGROUP && o->first) { o = o->first; continue; }
        if (!o->next && o->parent != select) o = o->parent;
        o = o->next;
    }
    return out;
}
/* Content-box size of a replaced element. */
static void replaced_size(WNode *n, int cw, int *w, int *h)
{
    const WStyle *s = n->style;
    int lh = line_height(s), fs = s->font.size;
    int iw = 0, ih = 0;
    switch (n->tag) {
    case T_CANVAS: case T_IMG:
        if (n->image) { iw = n->image->width * doc->zoom / 100; ih = n->image->height * doc->zoom / 100; }
        else {
            const char *alt = w_attr(n, "alt");
            if (alt && alt[0] && is_auto(&s->width)) { iw = (wf_text_width(&s->font, alt, w_len(alt), 0) + 15) / 16 + 4; ih = lh; }
        }
        break;
    case T_INPUT: {
        WControl *c = control_of(n);
        if (c->type == WC_CHECKBOX || c->type == WC_RADIO) { iw = ih = 13 * doc->zoom / 100; break; }
        if (c->type == WC_HIDDEN) break;
        if (c->type == WC_IMAGE) { if (n->image) { iw = n->image->width; ih = n->image->height; } else { iw = 60; ih = lh; } break; }
        if (c->type == WC_SUBMIT || c->type == WC_RESET || c->type == WC_BUTTON || c->type == WC_FILE) {
            const char *label = c->value[0] ? c->value : c->type == WC_RESET ? "Reset" : c->type == WC_FILE ? "Browse..." : "Submit";
            if (c->type == WC_BUTTON && !c->value[0]) label = "";
            iw = (wf_text_width(&s->font, label, w_len(label), 0) + 15) / 16; ih = lh;
            break;
        }
        const char *size = w_attr(n, "size");
        int chars = size ? (int)(size[0] - '0') : 20;
        if (size) { chars = 0; for (const char *q = size; *q >= '0' && *q <= '9'; q++) chars = chars * 10 + *q - '0'; }
        if (chars < 1) chars = 20;
        iw = chars * fs * 9 / 16; ih = lh;
        break;
    }
    case T_SELECT: {
        WControl *c = control_of(n);
        char text[256];
        int widest = 0;
        for (int i = 0; i < c->option_count && i < 64; i++) {
            option_text(n, i, text, sizeof(text));
            int tw = (wf_text_width(&s->font, text, w_len(text), 0) + 15) / 16;
            if (tw > widest) widest = tw;
        }
        iw = widest + 4; ih = lh;
        break;
    }
    case T_TEXTAREA: {
        control_of(n);
        const char *cols = w_attr(n, "cols"), *rows = w_attr(n, "rows");
        int c = 20, r = 2;
        if (cols) { c = 0; for (const char *q = cols; *q >= '0' && *q <= '9'; q++) c = c * 10 + *q - '0'; }
        if (rows) { r = 0; for (const char *q = rows; *q >= '0' && *q <= '9'; q++) r = r * 10 + *q - '0'; }
        if (c < 1) c = 20;
        if (r < 1) r = 2;
        iw = c * wf_advance(&s->font, 'm') / 16; ih = r * lh;
        break;
    }
    case T_SVG: case T_MATH: iw = 0; ih = 0; break;
    case T_AUDIO: iw = 300; ih = 32; break;
    default: iw = 300 * doc->zoom / 100; ih = 150 * doc->zoom / 100; break;
    }
    Edges b, p;
    edges(s, cw, 0, &b, &p);
    int extra_w = s->box_sizing ? b.l + b.r + p.l + p.r : 0, extra_h = s->box_sizing ? b.t + b.b + p.t + p.b : 0;
    int width_set = !is_auto(&s->width) && (s->width.pct == 0 || cw > 0);
    int height_set = !is_auto(&s->height) && s->height.pct == 0;
    int cw_value = width_set ? w_resolve(&s->width, cw, iw) - extra_w : iw;
    int ch_value = height_set ? w_resolve(&s->height, 0, ih) - extra_h : ih;
    if (width_set && !height_set && iw && n->tag == T_IMG && n->image) ch_value = ih * cw_value / iw;
    if (height_set && !width_set && ih && n->tag == T_IMG && n->image) cw_value = iw * ch_value / ih;
    if (s->max_width.kind != WL_NONE && (s->max_width.pct == 0 || cw > 0)) {
        int mw = w_resolve(&s->max_width, cw, cw_value) - extra_w;
        if (cw_value > mw && mw >= 0) { if (!height_set && iw && n->image) ch_value = ch_value * mw / (cw_value ? cw_value : 1); cw_value = mw; }
    }
    if (s->max_height.kind == WL_LEN && !s->max_height.pct) {
        int mh = s->max_height.px - extra_h;
        if (ch_value > mh && mh >= 0) { if (!width_set && ih && n->image) cw_value = cw_value * mh / (ch_value ? ch_value : 1); ch_value = mh; }
    }
    if (cw_value < 0) cw_value = 0;
    if (ch_value < 0) ch_value = 0;
    *w = cw_value; *h = ch_value;
}

/* ---------------------------------------------------------------- box decoration */
static void decorate(WNode *n, int x, int y, int w, int h)
{
    const WStyle *s = n->style;
    if (n->bg_item >= 0) {
        WItem *it = &doc->items[n->bg_item];
        it->x = x; it->y = y; it->w = w; it->h = h;
        int clip_padding = 0;
        (void)clip_padding;
    }
    if (n->border_item >= 0) {
        WItem *it = &doc->items[n->border_item];
        it->x = x; it->y = y; it->w = w; it->h = h;
    }
    (void)s;
}
static void begin_box(WNode *n)
{
    const WStyle *s = n->style;
    n->bg_item = n->border_item = -1;
    n->item_first = doc->item_count;
    if (((s->background >> 24) || s->has_gradient == 1) && !s->masked) {   /* masks (icon shapes) are not drawn */
        n->bg_item = doc->item_count;
        WItem *it = w_item(doc, WI_RECT);
        it->color = s->background; it->node = n; it->radius = s->radius;
        if (s->has_gradient == 1) { it->color = s->gradient[0]; it->color2 = s->gradient[1]; it->style = (unsigned char)(s->gradient_angle == 90 || s->gradient_angle == 270 ? 2 : 1); if (s->gradient_angle == 270 || s->gradient_angle == 0) { it->color = s->gradient[1]; it->color2 = s->gradient[0]; } }
    }
    int any = 0;
    for (int i = 0; i < 4; i++) if (s->border_width[i] && s->border_style[i] && (s->border_color[i] >> 24)) any = 1;
    if (any) {
        n->border_item = doc->item_count;
        WItem *it = w_item(doc, WI_BORDER);
        it->node = n; it->radius = s->radius;
        for (int i = 0; i < 4; i++) { it->widths[i] = s->border_style[i] ? s->border_width[i] : 0; it->colors[i] = s->border_color[i]; }
        it->style = s->border_style[0];
    }
    n->content_item = doc->item_count;
}
static void stretch_box(WNode *n, int height)
{
    if (height <= n->h) return;
    n->h = height;
    if (n->bg_item >= 0) doc->items[n->bg_item].h = height;
    if (n->border_item >= 0) doc->items[n->border_item].h = height;
}
static void finish_box(WNode *n)
{
    const WStyle *s = n->style;
    n->item_last = doc->item_count;
    if (s->opacity < 8) { for (int i = n->item_first; i < n->item_last; i++) doc->items[i].kind = WI_NONE; }
    else if (s->opacity < 255) {
        for (int i = n->item_first; i < n->item_last; i++) {
            WItem *it = &doc->items[i];
            it->color = ((it->color >> 24) * s->opacity / 255) << 24 | (it->color & 0xffffff);
            it->color2 = ((it->color2 >> 24) * s->opacity / 255) << 24 | (it->color2 & 0xffffff);
            for (int k = 0; k < 4; k++) it->colors[k] = ((it->colors[k] >> 24) * s->opacity / 255) << 24 | (it->colors[k] & 0xffffff);
            if (it->kind == WI_IMAGE || it->kind == WI_CONTROL) it->style = s->opacity < 128 ? 1 : 0;
        }
    }
    if (s->visibility) {   /* hidden: keep the space, draw nothing of its own */
        for (int i = n->item_first; i < n->content_item && i < n->item_last; i++) doc->items[i].kind = WI_NONE;
    }
}

/* ---------------------------------------------------------------- inline formatting */
enum { IT_TEXT, IT_SPACE, IT_BOX, IT_BREAK, IT_OPEN, IT_CLOSE, IT_SOFT };
typedef struct {
    unsigned char kind, preserved;
    WNode *node;
    const WStyle *st;
    const char *text; int length;
    int width;             /* 1/16 px */
    int above, below;      /* boxes */
    int first, last;       /* boxes: their items */
    WNode *link;
    int x;
} IItem;
static IItem *items;
static int item_count, item_capacity;
static int run_space;          /* the last item was collapsible space */
static WNode *marker_for;      /* the list item whose marker waits for a first line */
static int marker_number;
static IItem *add(int kind)
{
    if (item_count >= item_capacity) return 0;
    IItem *it = &items[item_count++];
    unsigned int *p = (unsigned int *)it;
    for (unsigned int i = 0; i < sizeof(IItem) / 4; i++) p[i] = 0;
    it->kind = (unsigned char)kind;
    it->link = current_link;
    return it;
}
static char *transform(const char *s, int length, int mode, int *out_length)
{
    char *out = web_alloc((unsigned)length * 2 + 4);
    const unsigned char *p = (const unsigned char *)s, *end = p + length;
    int n = 0, word_start = 1;
    while (p < end) {
        int c = w_utf8_decode(&p, end);
        int upper = mode == 1 || (mode == 3 && word_start);
        if (upper) {
            if (c >= 'a' && c <= 'z') c -= 32;
            else if (c >= 0x430 && c <= 0x44f) c -= 32;
            else if (c >= 0x450 && c <= 0x45f) c -= 80;
            else if (c >= 0xe0 && c <= 0xfe && c != 0xf7) c -= 32;
        } else if (mode == 2) {
            if (c >= 'A' && c <= 'Z') c += 32;
            else if (c >= 0x410 && c <= 0x42f) c += 32;
            else if (c >= 0x400 && c <= 0x40f) c += 80;
            else if (c >= 0xc0 && c <= 0xde && c != 0xd7) c += 32;
        }
        word_start = c == ' ' || c == '-' || c == '(' || c == '"';
        n += w_utf8_put(out + n, (unsigned)c);
    }
    *out_length = n;
    return out;
}
static void add_word(const WStyle *s, const char *text, int length, WNode *owner)
{
    if (!length) return;
    if (s->text_transform) text = transform(text, length, s->text_transform, &length);
    IItem *it = add(IT_TEXT);
    if (!it) return;
    it->st = s; it->node = owner; it->text = text; it->length = length;
    it->width = wf_text_width(&s->font, text, length, s->letter_spacing);
    run_space = 0;
}
static void add_space(const WStyle *s, int preserved, int count, WNode *owner)
{
    IItem *it = add(IT_SPACE);
    if (!it) return;
    it->st = s; it->node = owner; it->preserved = (unsigned char)preserved;
    it->width = (wf_advance(&s->font, ' ') + s->letter_spacing * 16 + s->word_spacing * 16) * count;
    if (!preserved) run_space = 1;
}
static void collect_text(WNode *t)
{
    const WStyle *s = t->style;
    int ws = s->white_space;
    int keep_spaces = ws == WS_PRE || ws == WS_PRE_WRAP, keep_lines = keep_spaces || ws == WS_PRE_LINE;
    const char *p = t->text, *end = p + t->length;
    while (p < end) {
        if (*p == '\n' || *p == '\r') {
            if (keep_lines) {
                if (*p == '\r' && p + 1 < end && p[1] == '\n') p++;
                p++;
                IItem *it = add(IT_BREAK); if (it) { it->st = s; it->node = t->parent; }
                run_space = 1;
                continue;
            }
            p++;
            if (!run_space && item_count) add_space(s, 0, 1, t->parent);
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\f') {
            if (keep_spaces) {
                int count = 0;
                while (p < end && (*p == ' ' || *p == '\t')) { count += *p == '\t' ? 8 : 1; p++; }
                add_space(s, ws == WS_PRE ? 2 : 1, count, t->parent);
                run_space = 0;
            } else {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\f' || ((*p == '\n' || *p == '\r') && !keep_lines))) p++;
                if (!run_space && item_count) add_space(s, ws == WS_NOWRAP ? 3 : 0, 1, t->parent);
            }
            continue;
        }
        const char *start = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f') {
            /* break opportunity after a hyphen or a slash in long words */
            if ((*p == '-' || *p == '/') && p - start > 2 && p + 1 < end && p[1] != ' ' && ws != WS_NOWRAP && ws != WS_PRE) { p++; break; }
            p++;
        }
        add_word(s, start, (int)(p - start), t->parent);
        if (p < end && p[-1] == '-' && *p != ' ') { IItem *soft = add(IT_SOFT); if (soft) soft->st = s; }
    }
}
static void collect(WNode *parent, WNode *first, WNode *stop, int cx, int cy, int cw);
static void collect_node(WNode *c, int cx, int cy, int cw)
{
    if (c->type == WN_TEXT) { collect_text(c); return; }
    const WStyle *s = c->style;
    if (!s || s->display == D_NONE) return;
    if (out_of_flow(c)) { defer(c, cx, cy); return; }
    if (c->tag == T_BR) { IItem *it = add(IT_BREAK); if (it) { it->st = s; it->node = c; } run_space = 1; return; }
    if (c->tag == T_WBR) { IItem *it = add(IT_SOFT); if (it) it->st = s; return; }
    if (s->floating) {   /* a float among the words: placed at the top of this run */
        layout_box(c, cx, cy, cw, -1, -1, -1);
        return;
    }
    WNode *saved_link = current_link;
    if (c->tag == T_A && w_attr(c, "href")) current_link = c;
    if (s->display == D_INLINE && !replaced(c)) {
        Edges m, b, p;
        edges(s, cw, &m, &b, &p);
        IItem *open = add(IT_OPEN);
        if (open) { open->node = c; open->st = s; open->width = (m.l + b.l + p.l) * 16; }
        collect(c, c->first, 0, cx, cy, cw);
        IItem *close = add(IT_CLOSE);
        if (close) { close->node = c; close->st = s; close->width = (m.r + b.r + p.r) * 16; }
        current_link = saved_link;
        return;
    }
    if (s->display == D_CONTENTS) { collect(c, c->first, 0, cx, cy, cw); current_link = saved_link; return; }
    /* an atomic box: laid out now at 0,0 and moved into its line later */
    int block_level = !inline_level(c);
    if (block_level) { IItem *br = add(IT_BREAK); if (br) { br->st = s; br->preserved = 1; } }   /* a block boundary, not a <br> */
    IItem *it = add(IT_BOX);
    if (!it) { current_link = saved_link; return; }
    int index = item_count - 1;
    int first = doc->item_count;
    Edges m;
    edges(s, cw, &m, 0, 0);
    int saved_base = float_base;
    float_base = float_count;
    int h = layout_box(c, 0, 0, cw, block_level ? -1 : -2, -1, -1);
    float_count = float_base; float_base = saved_base;
    it = &items[index];
    it->node = c; it->st = s;
    it->first = first; it->last = doc->item_count;
    it->width = (c->w + m.l + m.r) * 16;
    it->above = h + m.t + m.b; it->below = 0;
    if (s->vertical_align == VA_MIDDLE) { it->above = (h + m.t + m.b) / 2 + wf_ascent(&s->font) / 3; it->below = h + m.t + m.b - it->above; }
    if (block_level) { IItem *br = add(IT_BREAK); if (br) { br->st = s; br->preserved = 1; } }
    run_space = 0;
    current_link = saved_link;
}
static void collect(WNode *parent, WNode *first, WNode *stop, int cx, int cy, int cw)
{
    (void)parent;
    if (depth_now > MAX_DEPTH) return;
    depth_now++;
    for (WNode *c = first; c && c != stop; c = c->next) collect_node(c, cx, cy, cw);
    depth_now--;
}
static const char *roman(int v, int upper, char *out)
{
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *const lower[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    int n = 0;
    out[0] = 0;
    if (v <= 0 || v > 3999) { w_itoa(out, v); return out; }
    for (int i = 0; i < 13; i++) while (v >= values[i]) { for (const char *p = lower[i]; *p; p++) out[n++] = upper ? (char)(*p - 32) : *p; v -= values[i]; }
    out[n] = 0;
    return out;
}
static void emit_marker(WNode *li, int x, int baseline, int line_h)
{
    const WStyle *s = li->style;
    if (s->list_style == LS_NONE && !s->list_image) return;
    WItem *it = w_item(doc, WI_MARKER);
    it->node = li; it->color = s->color; it->size = (short)s->font.size; it->family = s->font.family;
    it->style = (unsigned char)(s->list_image && s->list_style == LS_NONE ? LS_DISC : s->list_style);
    it->y = baseline; it->h = line_h;
    if (it->style >= LS_DECIMAL) {
        char text[24];
        int v = marker_number;
        if (it->style == LS_DECIMAL) w_itoa(text, v);
        else if (it->style == LS_LOWER_ALPHA || it->style == LS_UPPER_ALPHA) { text[0] = (char)((it->style == LS_LOWER_ALPHA ? 'a' : 'A') + (v - 1) % 26); text[1] = 0; }
        else roman(v, it->style == LS_UPPER_ROMAN, text);
        w_append(text, ". ", sizeof(text));
        it->text = web_strdup(text, -1); it->length = w_len(text);
        WFont f = s->font;
        int width = (wf_text_width(&f, it->text, it->length, 0) + 15) / 16;
        it->x = x - width; it->w = width;
    } else { it->x = x - s->font.size; it->w = s->font.size; }
}
/* Lays out items[from..to) into lines from y; returns the height. */
typedef struct { WNode *node; int x, open_index; } Open;
static int place_lines(int from, int to, const WStyle *block, int cx, int cy, int cw, int bfc_floats)
{
    Open open[64];
    int open_count = 0;
    int y = cy, i = from, first_line = 1;
    int strut_asc = wf_ascent(&block->font), strut_desc = wf_descent(&block->font), strut_lh = line_height(block);
    int nowrap_block = block->white_space == WS_NOWRAP || block->white_space == WS_PRE;
    int bg_count = 0;
    (void)bfc_floats;
    while (i < to) {
        /* skip collapsible spaces at the start of a line */
        while (i < to && items[i].kind == IT_SPACE && !items[i].preserved) i++;
        if (i >= to) break;
        int left, right;
        float_space(y, strut_lh, cx, cx + cw, &left, &right);
        int indent = first_line ? px(&block->text_indent, cw) : 0;
        int avail = (right - left - indent) * 16;
        /* fit items */
        int used = 0, end = i, last_break = -1, pending_space = 0;
        int forced = 0;
        for (int k = i; k < to; k++) {
            IItem *it = &items[k];
            if (it->kind == IT_BREAK) { end = k + 1; forced = 1; break; }
            if (it->kind == IT_SPACE) {
                if (it->preserved == 2 || it->preserved == 3 || nowrap_block) { used += it->width; end = k + 1; continue; }
                pending_space += it->width;
                last_break = k;
                end = k + 1;
                continue;
            }
            if (it->kind == IT_SOFT) { last_break = k; end = k + 1; continue; }
            if (it->kind == IT_OPEN || it->kind == IT_CLOSE) { used += it->width; end = k + 1; continue; }
            int nowrap = it->st && (it->st->white_space == WS_NOWRAP || it->st->white_space == WS_PRE);
            int need = used + pending_space + it->width;
            int boxes = it->kind == IT_BOX || (k > i && items[k - 1].kind == IT_BOX);
            if (need > avail && end > i && (last_break >= i || (boxes && !nowrap)) && !(nowrap && last_break < i)) {
                if (boxes && !(last_break >= i && last_break == k - 1)) end = k;
                else end = last_break + 1;
                break;
            }
            if (need > avail && end == i && it->kind == IT_TEXT && (it->width > cw * 16 || it->st->word_break) && !nowrap) {
                /* a word wider than the box: split it where it fits */
                const unsigned char *p = (const unsigned char *)it->text, *e = p + it->length;
                int width = 0, cut = 0;
                while (p < e) {
                    const unsigned char *before = p;
                    int c = w_utf8_decode(&p, e);
                    int a = wf_advance(&it->st->font, (unsigned)c) + it->st->letter_spacing * 16;
                    if (width + a > avail - used && cut) { p = before; break; }
                    width += a; cut = (int)(p - (const unsigned char *)it->text);
                }
                if (cut > 0 && cut < it->length && item_count < item_capacity) {
                    /* the rest becomes the next item */
                    for (int m = item_count; m > k + 1; m--) items[m] = items[m - 1];
                    item_count++; to++;
                    items[k + 1] = *it;
                    items[k + 1].text = it->text + cut; items[k + 1].length = it->length - cut;
                    items[k + 1].width = wf_text_width(&it->st->font, items[k + 1].text, items[k + 1].length, it->st->letter_spacing);
                    it->length = cut; it->width = width;
                    used += pending_space + width;
                    end = k + 1;
                    break;
                }
            }
            used = need; pending_space = 0;
            end = k + 1;
        }
        if (end <= i) end = i + 1;
        /* trailing spaces do not count */
        int stop = end;
        while (stop > i && (items[stop - 1].kind == IT_SPACE && !items[stop - 1].preserved)) stop--;
        if (stop > i && items[stop - 1].kind == IT_BREAK) stop--;
        /* no room beside floats: move below them */
        int content_width = 0;
        for (int k = i; k < stop; k++) if (items[k].kind != IT_SPACE || items[k].preserved || k < stop) content_width += items[k].width;
        if (content_width > avail && (left > cx || right < cx + cw)) {
            int next = next_float_end(y);
            if (next > y) { y = next; continue; }
        }
        /* heights */
        int above = (strut_lh - (strut_asc + strut_desc)) / 2 + strut_asc, below = strut_lh - above;
        int line_empty = 1;
        for (int k = i; k < stop; k++) {
            IItem *it = &items[k];
            if (it->kind == IT_TEXT || (it->kind == IT_SPACE && it->preserved)) {
                const WStyle *s = it->st;
                int a = wf_ascent(&s->font), d = wf_descent(&s->font), lh = line_height(s);
                int top = (lh - (a + d)) / 2 + a, bottom = lh - top;
                int shift = s->vertical_align == VA_SUB ? s->font.size / 5 : s->vertical_align == VA_SUPER ? -s->font.size / 3 : 0;
                if (top - shift > above) above = top - shift;
                if (bottom + shift > below) below = bottom + shift;
                line_empty = 0;
            } else if (it->kind == IT_BOX) {
                if (it->st->vertical_align == VA_TOP || it->st->vertical_align == VA_BOTTOM) { if (it->above + it->below > above + below) below = it->above + it->below - above; }
                else { if (it->above > above) above = it->above; if (it->below > below) below = it->below; }
                line_empty = 0;
            } else if (it->kind == IT_OPEN || it->kind == IT_CLOSE) { if (it->width) line_empty = 0; }
        }
        if (line_empty && forced && items[end - 1].kind == IT_BREAK && items[end - 1].preserved == 1) {   /* nothing before a block */
            for (int k = i; k < end; k++) {
                if (items[k].kind == IT_OPEN && open_count < 64) { open[open_count].node = items[k].node; open[open_count].x = left; open[open_count].open_index = k; open_count++; }
                else if (items[k].kind == IT_CLOSE && open_count) open_count--;
            }
            i = end;
            continue;
        }
        if (line_empty && !forced) {
            int only_breaks = 1;
            for (int k = i; k < end; k++) if (items[k].kind != IT_SPACE && items[k].kind != IT_SOFT && items[k].kind != IT_OPEN && items[k].kind != IT_CLOSE) only_breaks = 0;
            if (only_breaks) {   /* nothing visible: no line at all */
                for (int k = i; k < end; k++) {
                    if (items[k].kind == IT_OPEN && open_count < 64) { open[open_count].node = items[k].node; open[open_count].x = left; open[open_count].open_index = k; open_count++; }
                    else if (items[k].kind == IT_CLOSE && open_count) open_count--;
                }
                i = end;
                continue;
            }
        }
        int line_h = above + below;
        int baseline = y + above;
        /* alignment */
        int extra = avail - used;
        int x16 = (left + indent) * 16;
        int align = block->text_align;
        int gaps = 0;
        if (align == TA_JUSTIFY && !forced && end < to) { for (int k = i; k < stop; k++) if (items[k].kind == IT_SPACE) gaps++; }
        else if (align == TA_RIGHT && extra > 0) x16 += extra;
        else if (align == TA_CENTER && extra > 0) x16 += extra / 2;
        int gap_extra = gaps && extra > 0 ? extra / gaps : 0;
        if (marker_for) { emit_marker(marker_for, cx - 6, baseline, line_h); marker_for = 0; }
        /* positions */
        for (int k = i; k < stop; k++) {
            IItem *it = &items[k];
            it->x = x16;
            if (it->kind == IT_SPACE) x16 += it->width + gap_extra;
            else x16 += it->width;
        }
        int line_end = x16;
        /* backgrounds of inline elements, outermost first */
        typedef struct { WNode *node; int x0, x1, index; } Segment;
        Segment segments[64];
        int segment_count = 0;
        for (int k = 0; k < open_count; k++) open[k].x = items[i].x;
        for (int k = i; k < end; k++) {
            IItem *it = &items[k];
            if (it->kind == IT_OPEN && open_count < 64) { open[open_count].node = it->node; open[open_count].x = k < stop ? it->x : line_end; open[open_count].open_index = k; open_count++; }
            else if (it->kind == IT_CLOSE && open_count) {
                open_count--;
                if (segment_count < 64) { segments[segment_count].node = open[open_count].node; segments[segment_count].x0 = open[open_count].x; segments[segment_count].x1 = (k < stop ? it->x : line_end) + it->width; segments[segment_count].index = open[open_count].open_index; segment_count++; }
            }
        }
        for (int k = 0; k < open_count && segment_count < 64; k++) {
            segments[segment_count].node = open[k].node; segments[segment_count].x0 = open[k].x; segments[segment_count].x1 = line_end; segments[segment_count].index = open[k].open_index; segment_count++;
        }
        for (int a = 1; a < segment_count; a++) { Segment v = segments[a]; int b = a; while (b > 0 && segments[b - 1].index > v.index) { segments[b] = segments[b - 1]; b--; } segments[b] = v; }
        for (int k = 0; k < segment_count; k++) {
            WNode *e = segments[k].node;
            const WStyle *s = e->style;
            Edges m, b, p;
            edges(s, cw, &m, &b, &p);
            int a = wf_ascent(&s->font), d = wf_descent(&s->font);
            int x0 = (segments[k].x0 + 8) / 16 + m.l, x1 = (segments[k].x1 + 8) / 16 - m.r;
            int top = baseline - a - p.t - b.t, h = a + d + p.t + p.b + b.t + b.b;
            if (e->x == 0 && e->y == 0 && e->w == 0) { e->x = x0; e->y = top; }
            if (x0 < e->x || e->w == 0) e->x = x0;
            if (x1 - e->x > e->w) e->w = x1 - e->x;
            if (top + h > e->y + e->h || e->h == 0) e->h = top + h - e->y;
            if (s->visibility) continue;
            if ((s->background >> 24) || s->has_gradient == 1) {
                WNode *saved = current_link; current_link = e->tag == T_A ? (w_attr(e, "href") ? e : current_link) : current_link;
                WItem *it = w_item(doc, WI_RECT);
                current_link = saved;
                it->x = x0; it->y = top; it->w = x1 - x0; it->h = h; it->color = s->background; it->node = e; it->radius = s->radius;
                it->fixed = 2;   /* moved to the front of the run later */
                bg_count++;
            }
            int any = 0;
            for (int q = 0; q < 4; q++) if (b.t + b.r + b.b + b.l && s->border_style[q]) any = 1;
            if (any) {
                WItem *it = w_item(doc, WI_BORDER);
                it->x = x0; it->y = top; it->w = x1 - x0; it->h = h; it->node = e; it->radius = s->radius; it->fixed = 2;
                for (int q = 0; q < 4; q++) { it->widths[q] = s->border_style[q] ? s->border_width[q] : 0; it->colors[q] = s->border_color[q]; }
                if (segments[k].x0 != items[segments[k].index].x || segments[k].index < i) it->widths[3] = 0;   /* continued from the line above */
                it->style = s->border_style[0];
                bg_count++;
            }
        }
        /* words and boxes */
        for (int k = i; k < stop; k++) {
            IItem *it = &items[k];
            if (it->kind == IT_TEXT) {
                const WStyle *s = it->st;
                if (s->visibility) continue;
                int opacity = 255;
                for (WNode *e = it->node; e && e->style && e->style->display == D_INLINE; e = e->parent) opacity = opacity * e->style->opacity / 255;
                int shift = s->vertical_align == VA_SUB ? s->font.size / 5 : s->vertical_align == VA_SUPER ? -s->font.size / 3 : 0;
                WNode *saved = current_link; current_link = it->link;
                WItem *t = w_item(doc, WI_TEXT);
                current_link = saved;
                t->x = (it->x + 8) / 16; t->y = baseline + shift; t->text = it->text; t->length = it->length;
                t->w = (it->width + 15) / 16; t->h = s->font.size;
                t->color = opacity < 255 ? ((s->color >> 24) * (unsigned)opacity / 255) << 24 | (s->color & 0xffffff) : s->color;
                t->size = (short)s->font.size; t->family = s->font.family; t->bold = s->font.bold; t->italic = s->font.italic;
                t->letter_spacing = (short)s->letter_spacing; t->node = it->node;
                if (s->decoration) {
                    int width = it->width;
                    if (k + 1 < stop && items[k + 1].kind == IT_SPACE && k + 2 < stop && items[k + 2].kind == IT_TEXT && items[k + 2].st->decoration) width = items[k + 2].x - it->x;
                    for (int bit = 1; bit <= 4; bit <<= 1) {
                        if (!(s->decoration & bit)) continue;
                        current_link = it->link;
                        WItem *l = w_item(doc, WI_LINE);
                        current_link = saved;
                        int thickness = s->font.size / 14 + 1;
                        l->x = t->x; l->w = (width + 15) / 16; l->h = thickness;
                        l->y = bit == TD_UNDERLINE ? baseline + shift + thickness + s->font.size / 12 : bit == TD_LINE_THROUGH ? baseline + shift - wf_ascent(&s->font) * 3 / 10 : baseline + shift - wf_ascent(&s->font);
                        l->color = s->decoration_color ? s->decoration_color : t->color;
                        l->node = it->node;
                    }
                }
            } else if (it->kind == IT_BOX) {
                Edges m;
                edges(it->st, cw, &m, 0, 0);
                int bx = (it->x + 8) / 16 + m.l, by;
                if (it->st->vertical_align == VA_TOP) by = y + m.t;
                else if (it->st->vertical_align == VA_BOTTOM) by = y + line_h - (it->above + it->below) + m.t;
                else by = baseline - it->above + m.t;
                shift_box(it->node, it->first, it->last, bx - it->node->x, by - it->node->y);
            }
        }
        /* keep inline elements that stay open for the next line */
        open_count = 0;
        for (int k = 0; k < segment_count; k++) {
            int closed = 0;
            for (int q = segments[k].index + 1; q < end; q++) if (items[q].kind == IT_CLOSE && items[q].node == segments[k].node) { closed = 1; break; }
            if (!closed && segments[k].index < end && open_count < 64) { open[open_count].node = segments[k].node; open[open_count].open_index = segments[k].index; open_count++; }
        }
        y += line_h;
        first_line = 0;
        i = end;
    }
    (void)bg_count;
    return y - cy;
}
/* Puts the inline backgrounds of a run before its words and boxes. */
static void backgrounds_first(int start)
{
    int count = doc->item_count - start, moved = 0;
    for (int i = start; i < doc->item_count; i++) if (doc->items[i].fixed == 2) moved++;
    if (!moved || moved == count) { for (int i = start; i < doc->item_count; i++) if (doc->items[i].fixed == 2) doc->items[i].fixed = 0; return; }
    unsigned int mark = web_mark();
    WItem *copy = web_alloc_big(sizeof(WItem) * (unsigned)count);
    if (!copy) { for (int i = start; i < doc->item_count; i++) if (doc->items[i].fixed == 2) doc->items[i].fixed = 0; return; }
    int a = 0, b = moved;
    for (int i = start; i < doc->item_count; i++) {
        WItem *it = &doc->items[i];
        if (it->fixed == 2) { it->fixed = 0; copy[a++] = *it; }
        else copy[b++] = *it;
    }
    for (int i = 0; i < count; i++) doc->items[start + i] = copy[i];
    web_release(mark);
}
static int inline_run(const WStyle *block, WNode *parent, WNode *first, WNode *stop, int cx, int cy, int cw)
{
    int base = item_count, start = doc->item_count;
    run_space = 1;
    collect(parent, first, stop, cx, cy, cw);
    int h = 0;
    if (item_count > base) h = place_lines(base, item_count, block, cx, cy, cw, 0);
    item_count = base;
    backgrounds_first(start);
    return h;
}

/* ---------------------------------------------------------------- block flow */
static int collapse(int a, int b)
{
    if (a >= 0 && b >= 0) return a > b ? a : b;
    if (a < 0 && b < 0) return a < b ? a : b;
    return a + b;
}
static int layout_flow(WNode *n, int cx, int cy, int cw, int containing_h)
{
    int y = cy, pending = 0;
    WNode *run = 0;
    int run_content = 0;
    WNode *stop_at = n->pseudo == 3 ? n->anon_stop : 0;
    for (WNode *c = n->first;; c = c->next) {
        if (c == stop_at) c = 0;
        int flush = !c;
        if (c) {
            if (hidden(c)) { continue; }
            if (inline_level(c) && !(c->type == WN_ELEMENT && c->style->display == D_CONTENTS && 0)) {
                if (!run) { run = c; run_content = 0; }
                if (!blank_text(c)) run_content = 1;
                continue;
            }
            if (out_of_flow(c) && run) { continue; }   /* stays in the run: its static position is the run's */
            flush = 1;
        }
        if (flush && run) {
            if (run_content) {
                y += pending; pending = 0;
                y += inline_run(n->style, n, run, c, cx, y, cw);
            } else {
                for (WNode *k = run; k != c; k = k->next) if (out_of_flow(k)) defer(k, cx, y + pending);
            }
            run = 0;
        }
        if (!c) break;
        if (out_of_flow(c)) { defer(c, cx, y + pending); continue; }
        const WStyle *s = c->style;
        Edges m;
        edges(s, cw, &m, 0, 0);
        if (s->floating) {
            layout_box(c, cx, y + pending, cw, -1, -1, containing_h);
            continue;
        }
        int top = collapse(pending, m.t);
        int at = y + top;
        if (s->clear) {
            int fb = float_bottom(s->clear == 1 ? 1 : s->clear == 2 ? 2 : 3);
            if (fb > at) at = fb;
        }
        int h = layout_box(c, cx, at - m.t, cw, -1, -1, containing_h);
        Edges b, p;
        edges(s, cw, 0, &b, &p);
        if (h == 0 && !b.t && !b.b && !p.t && !p.b && !(c->first) && c->tag != T_HR) {
            pending = collapse(top, m.b);   /* an empty block: its margins collapse through */
            if (s->clear) { y = at - top; }
            continue;
        }
        y = at + h;
        pending = m.b;
    }
    y += pending > 0 ? pending : 0;
    return y - cy;
}

/* ---------------------------------------------------------------- flexbox */
typedef struct { WNode *n; int base, main, min, max, grow, shrink, margin_main, margin_a, margin_b, auto_a, auto_b, cross, line; } FlexItem;
static int layout_flex(WNode *n, int cx, int cy, int cw, int definite_h)
{
    const WStyle *s = n->style;
    int row = s->flex_direction < 2, reverse = s->flex_direction == 1 || s->flex_direction == 3;
    int count = 0;
    for (WNode *c = n->first; c; c = c->next) count++;
    WNode **children = web_alloc(sizeof(WNode *) * (unsigned)(count + 1));
    count = box_children(n, children, count, cx, cy);
    FlexItem *fi = web_alloc(sizeof(FlexItem) * (unsigned)(count + 1));
    for (int i = 0; i < count; i++) fi[i].n = children[i];
    for (int i = 1; i < count; i++) {   /* order */
        FlexItem v = fi[i]; int j = i;
        while (j > 0 && fi[j - 1].n->style->order > v.n->style->order) { fi[j] = fi[j - 1]; j--; }
        fi[j] = v;
    }
    if (reverse) for (int i = 0; i < count / 2; i++) { FlexItem t = fi[i]; fi[i] = fi[count - 1 - i]; fi[count - 1 - i] = t; }
    int gap_main = row ? s->gap_column : s->gap_row, gap_cross = row ? s->gap_row : s->gap_column;
    int height = 0;
    if (row) {
        for (int i = 0; i < count; i++) {
            FlexItem *f = &fi[i];
            const WStyle *cs = f->n->style;
            Edges m, b, p;
            edges(cs, cw, &m, &b, &p);
            int extra = b.l + b.r + p.l + p.r;
            int imin, imax;
            intrinsic(f->n, &imin, &imax);
            int mm = (m.l > 0 ? m.l : 0) + (m.r > 0 ? m.r : 0);
            imin -= mm; imax -= mm;
            f->auto_a = cs->margin[3].kind == WL_AUTO; f->auto_b = cs->margin[1].kind == WL_AUTO;
            f->margin_a = m.l; f->margin_b = m.r;
            int base;
            if (cs->flex_basis.kind == WL_LEN || cs->flex_basis.kind >= WL_MIN) base = w_resolve(&cs->flex_basis, cw, imax) + (cs->box_sizing ? 0 : extra);
            else if (!is_auto(&cs->width)) base = w_resolve(&cs->width, cw, imax) + (cs->box_sizing ? 0 : extra);
            else base = imax;
            if (cs->flex_basis.kind == WL_LEN && cs->flex_basis.px == 0 && cs->flex_basis.pct == 0 && base < extra) base = extra;
            f->min = !is_auto(&cs->min_width) ? w_resolve(&cs->min_width, cw, 0) + (cs->box_sizing ? 0 : extra) : cs->overflow ? extra : imin;
            f->max = cs->max_width.kind != WL_NONE ? w_resolve(&cs->max_width, cw, 1 << 28) + (cs->box_sizing ? 0 : extra) : 1 << 28;
            if (f->min > f->max) f->min = f->max;
            if (!is_auto(&cs->width) && f->min > base) f->min = base;
            f->base = base;
            f->main = base < f->min ? f->min : base > f->max ? f->max : base;
            f->grow = cs->flex_grow; f->shrink = cs->flex_shrink;
            f->margin_main = m.l + m.r;
        }
        /* lines */
        int start = 0, y = cy;
        while (start < count) {
            int end = start, sum = 0;
            while (end < count) {
                int outer = fi[end].main + fi[end].margin_main + (end > start ? gap_main : 0);
                if (s->flex_wrap && end > start && sum + outer > cw) break;
                sum += outer; end++;
            }
            int free = cw - sum;
            for (int pass = 0; pass < 3; pass++) {   /* flexible lengths, with re-clamping */
                int total_grow = 0; long long total_shrink = 0;
                for (int i = start; i < end; i++) { total_grow += fi[i].grow; total_shrink += (long long)fi[i].shrink * fi[i].base; }
                if (free > 0 && total_grow > 0) {
                    int given = 0;
                    for (int i = start; i < end; i++) {
                        int share = (int)((long long)free * fi[i].grow / (total_grow < 100 ? 100 : total_grow));
                        if (total_grow < 100) share = free * fi[i].grow / 100;
                        int target = fi[i].main + share;
                        if (target > fi[i].max) target = fi[i].max;
                        given += target - fi[i].main;
                        fi[i].main = target;
                        if (target == fi[i].max) fi[i].grow = 0;
                    }
                    free -= given;
                    if (given == 0) break;
                } else if (free < 0 && total_shrink > 0) {
                    int taken = 0;
                    for (int i = start; i < end; i++) {
                        int cut = (int)((long long)-free * fi[i].shrink * fi[i].base / total_shrink);
                        int target = fi[i].main - cut;
                        if (target < fi[i].min) target = fi[i].min;
                        taken += fi[i].main - target;
                        fi[i].main = target;
                        if (target == fi[i].min) fi[i].shrink = 0;
                    }
                    free += taken;
                    if (taken == 0) break;
                } else break;
                if (free == 0) break;
            }
            /* auto margins take what is left */
            int autos = 0;
            for (int i = start; i < end; i++) autos += fi[i].auto_a + fi[i].auto_b;
            int lead = 0, between = 0;
            if (free > 0 && autos) {
                int each = free / autos;
                for (int i = start; i < end; i++) { if (fi[i].auto_a) fi[i].margin_a += each; if (fi[i].auto_b) fi[i].margin_b += each; }
                free = 0;
            }
            int items_in_line = end - start;
            int justify = s->justify;
            if (reverse && justify == JC_START) justify = JC_END;
            else if (reverse && justify == JC_END) justify = JC_START;
            if (free > 0) {
                if (justify == JC_END) lead = free;
                else if (justify == JC_CENTER) lead = free / 2;
                else if (justify == JC_BETWEEN && items_in_line > 1) between = free / (items_in_line - 1);
                else if (justify == JC_AROUND) { between = free / items_in_line; lead = between / 2; }
                else if (justify == JC_EVENLY) { between = free / (items_in_line + 1); lead = between; }
            }
            /* lay out at the final main size */
            int x = cx + lead, line_cross = 0;
            for (int i = start; i < end; i++) {
                FlexItem *f = &fi[i];
                const WStyle *cs = f->n->style;
                Edges m;
                edges(cs, cw, &m, 0, 0);
                int saved_base = float_base;
                float_base = float_count;
                int h = layout_box(f->n, x + f->margin_a - m.l, y, f->main + m.l + m.r, f->main, -1, definite_h);
                float_count = float_base; float_base = saved_base;
                f->cross = h + m.t + m.b;
                if (f->cross > line_cross) line_cross = f->cross;
                x += f->margin_a + f->main + f->margin_b + gap_main + between;
            }
            if (!s->flex_wrap && definite_h >= 0 && definite_h > line_cross) line_cross = definite_h;
            for (int i = start; i < end; i++) {
                FlexItem *f = &fi[i];
                const WStyle *cs = f->n->style;
                Edges m;
                edges(cs, cw, &m, 0, 0);
                int align = cs->align_self != 255 ? cs->align_self : s->align_items;
                if (align == JC_STRETCH && is_auto(&cs->height) && m.t >= 0 && cs->margin[0].kind != WL_AUTO && cs->margin[2].kind != WL_AUTO) stretch_box(f->n, line_cross - m.t - m.b);
                else if (cs->margin[0].kind == WL_AUTO && cs->margin[2].kind == WL_AUTO) shift_box(f->n, f->n->item_first, f->n->item_last, 0, (line_cross - f->cross) / 2);
                else if (align == JC_CENTER) shift_box(f->n, f->n->item_first, f->n->item_last, 0, (line_cross - f->cross) / 2);
                else if (align == JC_END) shift_box(f->n, f->n->item_first, f->n->item_last, 0, line_cross - f->cross);
            }
            y += line_cross + (end < count ? gap_cross : 0);
            start = end;
        }
        height = y - cy;
    } else {
        int y = cy;
        int total_grow = 0;
        for (int i = 0; i < count; i++) {
            FlexItem *f = &fi[i];
            const WStyle *cs = f->n->style;
            Edges m;
            edges(cs, cw, &m, 0, 0);
            int align = cs->align_self != 255 ? cs->align_self : s->align_items;
            int width = -1;
            int auto_margins = cs->margin[1].kind == WL_AUTO && cs->margin[3].kind == WL_AUTO;
            if ((align != JC_STRETCH || auto_margins) && is_auto(&cs->width)) {
                int imin, imax;
                intrinsic(f->n, &imin, &imax);
                width = imax - (m.l > 0 ? m.l : 0) - (m.r > 0 ? m.r : 0);
                if (width > cw - m.l - m.r) width = cw - m.l - m.r;
            }
            int h;
            int saved_base = float_base;
            float_base = float_count;
            int basis = -1;
            if (cs->flex_basis.kind == WL_LEN && (cs->flex_basis.px || cs->flex_basis.pct) && definite_h >= 0) basis = w_resolve(&cs->flex_basis, definite_h, 0);
            if (width >= 0) {
                int item_w = width + m.l + m.r;
                int x = cx;
                if (auto_margins || align == JC_CENTER) x = cx + (cw - item_w) / 2;
                else if (align == JC_END) x = cx + cw - item_w;
                h = layout_box(f->n, x, y, item_w, width, basis, -1);
            } else h = layout_box(f->n, cx, y, cw, -1, basis, -1);
            float_count = float_base; float_base = saved_base;
            f->main = h; f->margin_main = m.t + m.b;
            f->grow = cs->flex_grow;
            total_grow += cs->flex_grow;
            y += m.t + h + m.b + (i + 1 < count ? gap_main : 0);
        }
        int used = y - cy;
        int free = definite_h >= 0 ? definite_h - used : 0;
        if (free > 0 && total_grow > 0) {
            int moved = 0;
            for (int i = 0; i < count; i++) {
                FlexItem *f = &fi[i];
                if (moved) shift_box(f->n, f->n->item_first, f->n->item_last, 0, moved);
                int share = total_grow < 100 ? free * f->grow / 100 : free * f->grow / total_grow;
                if (share > 0) { stretch_box(f->n, f->n->h + share); moved += share; }
            }
            free -= moved;
            used += moved;
        }
        if (free > 0 && (s->justify == JC_CENTER || s->justify == JC_END || s->justify == JC_BETWEEN || s->justify == JC_AROUND || s->justify == JC_EVENLY)) {
            int lead = s->justify == JC_END ? free : s->justify == JC_CENTER ? free / 2 : 0, between = 0;
            if (s->justify == JC_BETWEEN && count > 1) between = free / (count - 1);
            if (s->justify == JC_AROUND && count) { between = free / count; lead = between / 2; }
            if (s->justify == JC_EVENLY) { between = free / (count + 1); lead = between; }
            for (int i = 0; i < count; i++) shift_box(fi[i].n, fi[i].n->item_first, fi[i].n->item_last, 0, lead + between * i);
        }
        height = used;
    }
    return height;
}

/* ---------------------------------------------------------------- grid */
static int layout_grid(WNode *n, int cx, int cy, int cw)
{
    const WStyle *s = n->style;
    int gap_c = s->gap_column, gap_r = s->gap_row;
    int columns = s->column_count > 0 ? s->column_count : 1;
    int widths[W_TRACKS * 4];
    WTrack tracks[W_TRACKS * 4];
    if (s->column_auto_fill) {
        int min = w_resolve(&s->column_auto_min, cw, 100);
        if (min < 1) min = 100;
        columns = (cw + gap_c) / (min + gap_c);
        if (columns < 1) columns = 1;
        if (columns > W_TRACKS * 4) columns = W_TRACKS * 4;
        for (int i = 0; i < columns; i++) { tracks[i] = s->columns[0]; tracks[i].fr = 100; }
    } else if (s->column_count > 0) {
        for (int i = 0; i < columns; i++) tracks[i] = s->columns[i];
    } else { WLen a = {WL_AUTO, 0, 0, 0}; tracks[0].min = a; tracks[0].max = a; tracks[0].fr = 100; }
    /* fixed, then auto (widest single-column item), then fr */
    int used = gap_c * (columns - 1), fr_total = 0;
    int auto_cols[W_TRACKS * 4];
    for (int i = 0; i < columns; i++) {
        auto_cols[i] = 0;
        if (tracks[i].fr) { fr_total += tracks[i].fr; widths[i] = tracks[i].min.kind == WL_LEN ? w_resolve(&tracks[i].min, cw, 0) : 0; used += widths[i]; continue; }
        if (tracks[i].max.kind == WL_LEN || tracks[i].max.kind >= WL_MIN) { widths[i] = w_resolve(&tracks[i].max, cw, 0); used += widths[i]; continue; }
        auto_cols[i] = 1; widths[i] = 0;
    }
    /* item placement */
    int count = 0;
    for (WNode *c = n->first; c; c = c->next) count++;
    WNode **children = web_alloc(sizeof(WNode *) * (unsigned)(count + 1));
    count = box_children(n, children, count, cx, cy);
    WNode **cells = web_alloc(sizeof(WNode *) * (unsigned)(count + 1));
    int *col_of = web_alloc(sizeof(int) * (unsigned)(count + 1)), *span_of = web_alloc(sizeof(int) * (unsigned)(count + 1)), *row_of = web_alloc(sizeof(int) * (unsigned)(count + 1));
    int k = 0, row = 0, col = 0;
    for (int ci = 0; ci < count; ci++) {
        WNode *c = children[ci];
        int span = c->style->column_span;
        if (span < 0 || span > columns) span = columns;
        if (span < 1) span = 1;
        int start = c->style->column_start;
        if (start > 0 && start <= columns) { if (start - 1 < col) row++; col = start - 1; }
        if (col + span > columns) { row++; col = 0; }
        cells[k] = c; col_of[k] = col; span_of[k] = span; row_of[k] = row;
        k++;
        col += span;
        if (col >= columns) { col = 0; row++; }
    }
    count = k;
    for (int i = 0; i < count; i++) {
        if (span_of[i] != 1 || !auto_cols[col_of[i]]) continue;
        int imin, imax;
        intrinsic(cells[i], &imin, &imax);
        if (imax > widths[col_of[i]]) widths[col_of[i]] = imax;
    }
    for (int i = 0; i < columns; i++) if (auto_cols[i]) used += widths[i];
    int free = cw - used;
    if (free < 0 && !fr_total) {   /* shrink auto columns */
        int auto_total = 0;
        for (int i = 0; i < columns; i++) if (auto_cols[i]) auto_total += widths[i];
        for (int i = 0; i < columns && auto_total; i++) if (auto_cols[i]) widths[i] += (int)((long long)free * widths[i] / auto_total);
    }
    if (fr_total && free > 0) {
        for (int i = 0; i < columns; i++) if (tracks[i].fr) widths[i] += (int)((long long)free * tracks[i].fr / fr_total);
    } else if (!fr_total && free > 0) {
        int autos = 0;
        for (int i = 0; i < columns; i++) autos += auto_cols[i];
        for (int i = 0; i < columns && autos; i++) if (auto_cols[i]) widths[i] += free / autos;
    }
    int lead = 0;
    if (!fr_total && free > 0 && s->justify == JC_CENTER) lead = free / 2;
    int xs[W_TRACKS * 4 + 1];
    xs[0] = cx + lead;
    for (int i = 0; i < columns; i++) xs[i + 1] = xs[i] + widths[i] + gap_c;
    int y = cy;
    for (int i = 0; i < count;) {
        int r = row_of[i], j = i, row_h = 0;
        while (j < count && row_of[j] == r) {
            int c0 = col_of[j], w = xs[c0 + span_of[j]] - xs[c0] - gap_c;
            Edges m;
            edges(cells[j]->style, w, &m, 0, 0);
            int saved_base = float_base;
            float_base = float_count;
            int h = layout_box(cells[j], xs[c0], y, w, w - m.l - m.r, -1, -1);
            float_count = float_base; float_base = saved_base;
            if (h + m.t + m.b > row_h) row_h = h + m.t + m.b;
            j++;
        }
        for (int q = i; q < j; q++) {
            const WStyle *cs = cells[q]->style;
            Edges m;
            edges(cs, cw, &m, 0, 0);
            int align = cs->align_self != 255 ? cs->align_self : s->align_items;
            if (align == JC_STRETCH && is_auto(&cs->height)) stretch_box(cells[q], row_h - m.t - m.b);
            else if (align == JC_CENTER) shift_box(cells[q], cells[q]->item_first, cells[q]->item_last, 0, (row_h - cells[q]->h - m.t - m.b) / 2);
            else if (align == JC_END) shift_box(cells[q], cells[q]->item_first, cells[q]->item_last, 0, row_h - cells[q]->h - m.t - m.b);
        }
        y += row_h + (j < count ? gap_r : 0);
        i = j;
    }
    return y - cy;
}

/* ---------------------------------------------------------------- tables */
#define MAX_COLUMNS 48
static int layout_table(WNode *n, int cx, int cy, int cw, int fixed_width)
{
    const WStyle *s = n->style;
    int spacing = s->border_collapse ? 0 : s->border_spacing;
    /* rows in order: header groups, bodies and loose rows, footers */
    int row_capacity = 64, row_count = 0;
    WNode **rows = web_alloc(sizeof(WNode *) * (unsigned)row_capacity);
    WNode *captions[4]; int caption_count = 0;
    for (int pass = 0; pass < 3; pass++)
        for (WNode *c = n->first; c; c = c->next) {
            if (c->type != WN_ELEMENT || hidden(c)) continue;
            int d = c->style->display;
            if (pass == 0 && d == D_TABLE_CAPTION && caption_count < 4) captions[caption_count++] = c;
            int group = d == D_TABLE_ROW_GROUP;
            int which = group ? (c->tag == T_THEAD ? 0 : c->tag == T_TFOOT ? 2 : 1) : 1;
            if (which != pass) continue;
            if (!group && d != D_TABLE_ROW && c->tag != T_TR && d != D_TABLE_CAPTION && d != D_TABLE_COLUMN && !out_of_flow(c)) {
                /* other content: an anonymous row with one anonymous cell around it */
                WNode *start = c;
                int content = 0;
                while (c && !(c->type == WN_ELEMENT && (hidden(c) || c->style->display == D_TABLE_ROW || c->style->display == D_TABLE_ROW_GROUP ||
                                                        c->tag == T_TR || c->style->display == D_TABLE_CAPTION))) {
                    if (!blank_text(c)) content = 1;
                    c = c->next;
                }
                if (content && row_count < row_capacity) {
                    WNode *cell = anonymous(n, start, c);
                    cell->style->display = D_TABLE_CELL;
                    rows[row_count++] = cell;
                }
                if (!c) break;
                c = c->prev;   /* the loop's c->next is the first node after the group */
                continue;
            }
            if (group) {
                for (WNode *r = c->first; r; r = r->next) if (r->type == WN_ELEMENT && !hidden(r) && (r->style->display == D_TABLE_ROW || r->tag == T_TR)) {
                    if (row_count == row_capacity) { WNode **b = web_alloc(sizeof(WNode *) * (unsigned)row_capacity * 2); for (int i = 0; i < row_count; i++) b[i] = rows[i]; rows = b; row_capacity *= 2; }
                    rows[row_count++] = r;
                }
            } else if (d == D_TABLE_ROW || c->tag == T_TR) {
                if (row_count == row_capacity) { WNode **b = web_alloc(sizeof(WNode *) * (unsigned)row_capacity * 2); for (int i = 0; i < row_count; i++) b[i] = rows[i]; rows = b; row_capacity *= 2; }
                rows[row_count++] = c;
            }
        }
    /* columns */
    int columns = 0;
    int mins[MAX_COLUMNS], maxs[MAX_COLUMNS], fixed[MAX_COLUMNS];
    for (int i = 0; i < MAX_COLUMNS; i++) mins[i] = maxs[i] = fixed[i] = 0;
    for (int r = 0; r < row_count; r++) {
        int col = 0;
        WNode *only = rows[r]->pseudo == 3 ? rows[r] : 0;   /* an anonymous cell is its own row */
        for (WNode *c = only ? only : rows[r]->first; c; c = only ? 0 : c->next) {
            if (c->type != WN_ELEMENT || hidden(c)) continue;
            const char *cs = only ? 0 : w_attr(c, "colspan");
            int span = cs ? (cs[0] - '0') : 1;
            if (cs) { span = 0; for (const char *q = cs; *q >= '0' && *q <= '9'; q++) span = span * 10 + *q - '0'; }
            if (span < 1) span = 1;
            if (col + span > MAX_COLUMNS) span = MAX_COLUMNS - col;
            if (span < 1) break;
            int imin, imax;
            intrinsic(c, &imin, &imax);
            int width_set = c->style->width.kind == WL_LEN && !c->style->width.pct;
            if (span == 1) {
                if (imin > mins[col]) mins[col] = imin;
                if (imax > maxs[col]) maxs[col] = imax;
                if (width_set && imax > fixed[col]) fixed[col] = imax;
            } else {
                int have_min = 0, have_max = 0;
                for (int k = col; k < col + span; k++) { have_min += mins[k]; have_max += maxs[k]; }
                have_min += spacing * (span - 1); have_max += spacing * (span - 1);
                if (imin > have_min) for (int k = col; k < col + span; k++) mins[k] += (imin - have_min) / span;
                if (imax > have_max) for (int k = col; k < col + span; k++) maxs[k] += (imax - have_max) / span;
            }
            col += span;
        }
        if (col > columns) columns = col;
    }
    if (columns > MAX_COLUMNS) columns = MAX_COLUMNS;
    for (int i = 0; i < columns; i++) { if (fixed[i] && fixed[i] > mins[i]) { mins[i] = fixed[i]; } if (maxs[i] < mins[i]) maxs[i] = mins[i]; }
    int sum_min = spacing * (columns + 1), sum_max = spacing * (columns + 1);
    for (int i = 0; i < columns; i++) { sum_min += mins[i]; sum_max += maxs[i]; }
    int target = fixed_width >= 0 ? fixed_width : (sum_max < cw ? sum_max : cw);
    if (target < sum_min) target = sum_min;
    int widths[MAX_COLUMNS];
    if (target >= sum_max) {
        int extra = target - sum_max, total = sum_max - spacing * (columns + 1);
        for (int i = 0; i < columns; i++) widths[i] = maxs[i] + (total > 0 ? (int)((long long)extra * maxs[i] / total) : extra / (columns ? columns : 1));
    } else {
        int range = sum_max - sum_min, room = target - sum_min;
        for (int i = 0; i < columns; i++) widths[i] = mins[i] + (range > 0 ? (int)((long long)room * (maxs[i] - mins[i]) / range) : 0);
    }
    int y = cy;
    for (int i = 0; i < caption_count; i++) if (!captions[i]->style->caption_bottom) y += layout_box(captions[i], cx, y, target, target, -1, -1);
    y += spacing;
    int xs[MAX_COLUMNS + 1];
    xs[0] = cx + spacing;
    for (int i = 0; i < columns; i++) xs[i + 1] = xs[i] + widths[i] + spacing;
    for (int r = 0; r < row_count; r++) {
        WNode *row = rows[r];
        if (row->pseudo == 3) {   /* an anonymous cell across all columns */
            int w = xs[columns > 0 ? columns : 0] - xs[0] - spacing;
            if (w < 0 || columns == 0) w = target - 2 * spacing;
            int saved_base = float_base;
            float_base = float_count;
            int h = layout_box(row, xs[0], y, w, w, -1, -1);
            float_count = float_base; float_base = saved_base;
            y += h + spacing;
            continue;
        }
        begin_box(row);
        row->x = cx; row->y = y; row->w = target;
        int col = 0, row_h = 0;
        if (row->style->height.kind == WL_LEN && !row->style->height.pct) row_h = row->style->height.px;
        WNode *cells[MAX_COLUMNS]; int cell_count = 0, natural[MAX_COLUMNS];
        for (WNode *c = row->first; c; c = c->next) {
            if (c->type != WN_ELEMENT || hidden(c)) continue;
            const char *cs = w_attr(c, "colspan");
            int span = 1;
            if (cs) { span = 0; for (const char *q = cs; *q >= '0' && *q <= '9'; q++) span = span * 10 + *q - '0'; }
            if (span < 1) span = 1;
            if (col + span > columns) span = columns - col;
            if (span < 1) break;
            int w = xs[col + span] - xs[col] - spacing;
            int saved_base = float_base;
            float_base = float_count;
            int h = layout_box(c, xs[col], y, w, w, -1, -1);
            float_count = float_base; float_base = saved_base;
            if (cell_count < MAX_COLUMNS) { natural[cell_count] = h; cells[cell_count++] = c; }
            if (h > row_h) row_h = h;
            col += span;
        }
        for (int i = 0; i < cell_count; i++) {
            WNode *c = cells[i];
            int free = row_h - natural[i];
            stretch_box(c, row_h);
            int va = c->style->vertical_align;
            int dy = va == VA_MIDDLE ? free / 2 : va == VA_BOTTOM ? free : 0;
            if (dy > 0) { shift_items(c->content_item, c->item_last, 0, dy); for (WNode *k = c->first; k; k = k->next) shift_nodes(k, 0, dy); }
        }
        row->h = row_h;
        decorate(row, cx + spacing, y, target - 2 * spacing, row_h);
        finish_box(row);
        y += row_h + spacing;
    }
    for (int i = 0; i < caption_count; i++) if (captions[i]->style->caption_bottom) y += layout_box(captions[i], cx, y, target, target, -1, -1);
    (void)s;
    return y - cy;
}

/* ---------------------------------------------------------------- boxes */
static int establishes_bfc(const WNode *n)
{
    const WStyle *s = n->style;
    return s->overflow || s->floating || s->display == D_FLOW_ROOT || s->display == D_INLINE_BLOCK || s->display == D_TABLE_CELL ||
           s->display == D_FLEX || s->display == D_GRID || s->display == D_TABLE || out_of_flow(n) || n->tag == T_HTML || n->tag == T_BODY ||
           (n->parent && n->parent->style && (n->parent->style->display == D_FLEX || n->parent->style->display == D_GRID || n->parent->style->display == D_INLINE_FLEX));
}
/* Lays out n with its margin box starting at x, y inside a containing
 * block avail wide. forced_w: the border-box width (-1 auto, -2 shrink to
 * fit); forced_h: the border-box height or -1. Returns the border-box
 * height; n->x/y/w/h get the border box. */
static int layout_box(WNode *n, int x, int y, int avail, int forced_w, int forced_h, int containing_h)
{
    const WStyle *s = n->style;
    if (depth_now > MAX_DEPTH) { n->x = x; n->y = y; n->w = 0; n->h = 0; return 0; }
    depth_now++;
    Edges m, b, p;
    edges(s, avail, &m, &b, &p);
    int extra_w = b.l + b.r + p.l + p.r, extra_h = b.t + b.b + p.t + p.b;
    int is_float = s->floating != 0;
    int shrink = forced_w == -2 || is_float || out_of_flow(n) || s->display == D_INLINE_BLOCK || s->display == D_INLINE_TABLE ||
                 s->display == D_INLINE_FLEX || s->display == D_INLINE_GRID || ((s->display == D_TABLE) && forced_w < 0) || n->tag == T_BUTTON;
    int auto_l = s->margin[3].kind == WL_AUTO, auto_r = s->margin[1].kind == WL_AUTO;
    int content_w;
    int is_replaced = replaced(n);
    int replaced_h = 0;
    if (forced_w >= 0) content_w = forced_w - extra_w;
    else if (is_replaced) replaced_size(n, avail, &content_w, &replaced_h);
    else if (!is_auto(&s->width) && (s->width.pct == 0 || avail > 0)) content_w = w_resolve(&s->width, avail, 0) - (s->box_sizing ? extra_w : 0);
    else if (shrink) {
        int imin, imax;
        intrinsic(n, &imin, &imax);
        int room = avail - (m.l > 0 ? m.l : 0) - (m.r > 0 ? m.r : 0);
        int mm = (m.l > 0 ? m.l : 0) + (m.r > 0 ? m.r : 0);
        imin -= mm; imax -= mm;
        content_w = (imax < room ? imax : room > imin ? room : imin) - extra_w;
    } else content_w = avail - m.l - m.r - extra_w;
    if (forced_w < 0 && !is_replaced) {
        if (s->max_width.kind != WL_NONE && (s->max_width.pct == 0 || avail > 0)) {
            int mw = w_resolve(&s->max_width, avail, content_w) - (s->box_sizing ? extra_w : 0);
            if (content_w > mw) content_w = mw;
        }
        if (!is_auto(&s->min_width)) {
            int mw = w_resolve(&s->min_width, avail, 0) - (s->box_sizing ? extra_w : 0);
            if (content_w < mw) content_w = mw;
        }
    }
    if (content_w < 0) content_w = 0;
    int border_w = content_w + extra_w;
    /* auto margins center a block with a width */
    if (forced_w == -1 && !shrink && !is_float && s->display != D_INLINE_BLOCK) {
        int free = avail - border_w - (auto_l ? 0 : m.l) - (auto_r ? 0 : m.r);
        if (auto_l && auto_r) { m.l = free > 0 ? free / 2 : 0; m.r = m.l; }
        else if (auto_l) m.l = free > 0 ? free : 0;
        else if (auto_r) m.r = free > 0 ? free : 0;
    } else if (forced_w == -1 && s->display == D_TABLE && auto_l && auto_r) {
        int free = avail - border_w;
        m.l = free > 0 ? free / 2 : 0;
    }
    if (forced_w == -1 && !is_float && !out_of_flow(n) && n->parent && n->parent->style && n->parent->style->center_blocks &&
        !auto_l && !auto_r && m.l == 0 && m.r == 0 && !inline_level(n)) {   /* <center>: blocks are centered too */
        int free = avail - border_w;
        if (free > 0) m.l = free / 2;
    }
    int bx = x + m.l, by = y + m.t;
    if (is_float) {   /* floats sit beside earlier floats */
        int outer_w = border_w + m.l + m.r, fy = y;
        if (s->clear) { int fb = float_bottom(s->clear == 1 ? 1 : s->clear == 2 ? 2 : 3); if (fb > fy) fy = fb; }
        for (int tries = 0; tries < 64; tries++) {
            int left, right;
            float_space(fy, 1, x, x + avail, &left, &right);
            if (right - left >= outer_w || (left == x && right == x + avail)) {
                bx = s->floating == 1 ? left + m.l : right - outer_w + m.l;
                break;
            }
            int next = next_float_end(fy);
            if (next < 0) break;
            fy = next;
        }
        by = fy + m.t;
    }
    n->x = bx; n->y = by; n->w = border_w;
    begin_box(n);
    WNode *saved_link = current_link;
    if (n->tag == T_A && w_attr(n, "href")) current_link = n;
    int clip = s->overflow && n->tag != T_HTML && n->tag != T_BODY ? doc->item_count : -1;
    if (clip >= 0) { WItem *it = w_item(doc, WI_CLIP); it->node = n; }
    int cx = bx + b.l + p.l, cy = by + b.t + p.t;
    int definite_h = -1;
    if (forced_h >= 0) definite_h = forced_h - extra_h;
    else if (!is_auto(&s->height) && (s->height.pct == 0 || containing_h >= 0)) definite_h = w_resolve(&s->height, containing_h, 0) - (s->box_sizing ? extra_h : 0);
    if (definite_h >= 0 && !is_auto(&s->min_height) && s->min_height.pct == 0) { int mh = s->min_height.px - (s->box_sizing ? extra_h : 0); if (definite_h < mh) definite_h = mh; }
    if (n->tag == T_HTML || n->tag == T_BODY) definite_h = -1;
    int saved_float_base = float_base, saved_float_count = float_count;
    int bfc = establishes_bfc(n);
    if (bfc) float_base = float_count;
    WNode *saved_marker = marker_for;
    int saved_number = marker_number;
    if (s->display == D_LIST_ITEM) {
        /* number: the list's start (or a value= on an earlier item) plus the items before */
        int base = 1, before = 0;
        const char *start = n->parent ? w_attr(n->parent, "start") : 0;
        if (start) base = number_of(start);
        for (WNode *q = n->prev; q; q = q->prev)
            if (q->type == WN_ELEMENT && q->style && q->style->display == D_LIST_ITEM) {
                before++;
                const char *v = w_attr(q, "value");
                if (v) { base = number_of(v); break; }
            }
        const char *value = w_attr(n, "value");
        marker_number = value ? number_of(value) : base + before;
        marker_for = n;
    }
    int content_h;
    if (is_replaced) {
        content_h = replaced_h;
        if (forced_w >= 0 || !is_auto(&s->width)) { int w2, h2; replaced_size(n, avail, &w2, &h2); content_h = h2; if (n->image && w2 && forced_w >= 0 && is_auto(&s->height)) content_h = h2 * content_w / w2; }
        WItem *it = w_item(doc, (n->tag == T_IMG || n->tag == T_CANVAS) && n->image ? WI_IMAGE : WI_CONTROL);
        it->node = n; it->x = cx; it->y = cy; it->w = content_w; it->h = content_h; it->image = n->image;
        it->color = s->color; it->size = (short)s->font.size; it->family = s->font.family;
        if (n->tag == T_INPUT && n->control && n->control->type == WC_IMAGE && n->image) { it->kind = WI_IMAGE; }
        if (n->tag == T_IMG && !n->image) { it->text = w_attr(n, "alt"); it->length = w_len(it->text); }
    } else {
        int d = s->display;
        if (d == D_FLEX || d == D_INLINE_FLEX) content_h = layout_flex(n, cx, cy, content_w, definite_h);
        else if (d == D_GRID || d == D_INLINE_GRID) content_h = layout_grid(n, cx, cy, content_w);
        else if (d == D_TABLE || d == D_INLINE_TABLE) {
            int fixed = !is_auto(&s->width) && forced_w < 0 ? content_w : forced_w >= 0 ? content_w : -1;
            content_h = layout_table(n, cx, cy, content_w, fixed);
        } else content_h = layout_flow(n, cx, cy, content_w, definite_h);
    }
    if (s->display == D_LIST_ITEM) {
        if (marker_for == n) emit_marker(n, cx - 6, cy + wf_ascent(&s->font), line_height(s));   /* no line inside: at the top */
        marker_for = saved_marker;
        marker_number = saved_number;
    }
    if (bfc) {
        int fb = float_bottom(3);
        if (fb - cy > content_h) content_h = fb - cy;
    }
    if (definite_h >= 0 && !(is_replaced)) content_h = definite_h;
    else if (!is_replaced) {
        if (!is_auto(&s->min_height) && (s->min_height.pct == 0 || containing_h >= 0)) { int mh = w_resolve(&s->min_height, containing_h, 0) - (s->box_sizing ? extra_h : 0); if (content_h < mh) content_h = mh; }
        if (s->max_height.kind != WL_NONE && (s->max_height.pct == 0 || containing_h >= 0)) { int mh = w_resolve(&s->max_height, containing_h, content_h) - (s->box_sizing ? extra_h : 0); if (content_h > mh) content_h = mh; }
    }
    int border_h = content_h + extra_h;
    if (forced_h >= 0) border_h = forced_h;
    if (border_h < 0) border_h = 0;
    n->h = border_h;
    if (bfc) { float_count = saved_float_count; float_base = saved_float_base; }
    if (is_float) {
        if (float_count < 512) { Float *f = &floats[float_count++]; f->x = bx - m.l; f->y = by - m.t; f->w = border_w + m.l + m.r; f->h = border_h + m.t + m.b; f->side = s->floating; }
    }
    if (clip >= 0) {
        WItem *it = &doc->items[clip];
        it->x = bx + b.l; it->y = by + b.t; it->w = border_w - b.l - b.r; it->h = border_h - b.t - b.b;
        w_item(doc, WI_UNCLIP);
    }
    decorate(n, bx, by, border_w, border_h);
    current_link = saved_link;
    finish_box(n);
    if (s->position == P_RELATIVE || s->position == P_STICKY) {
        int dx = 0, dy = 0;
        if (s->position == P_RELATIVE) {
            if (!is_auto(&s->offset[3])) dx = w_resolve(&s->offset[3], avail, 0);
            else if (!is_auto(&s->offset[1])) dx = -w_resolve(&s->offset[1], avail, 0);
            if (!is_auto(&s->offset[0])) dy = w_resolve(&s->offset[0], containing_h > 0 ? containing_h : 0, 0);
            else if (!is_auto(&s->offset[2])) dy = -w_resolve(&s->offset[2], containing_h > 0 ? containing_h : 0, 0);
        }
        if (dx || dy) shift_box(n, n->item_first, n->item_last, dx, dy);
    }
    depth_now--;
    return border_h;
}

/* Absolutely positioned and fixed boxes, after the normal flow. */
static void layout_deferred(void)
{
    for (int i = 0; i < deferred_count; i++) {
        WNode *n = deferred[i].n;
        const WStyle *s = n->style;
        int fixed = s->position == P_FIXED;
        int cbx = 0, cby = 0, cbw = viewport_w, cbh = viewport_h;
        if (!fixed) {
            WNode *cb = n->parent;
            while (cb && cb->style && cb->style->position == P_STATIC && cb->tag != T_HTML) cb = cb->parent;
            if (cb && cb->style && cb->tag != T_HTML && cb->w) {
                cbx = cb->x + cb->style->border_width[3]; cby = cb->y + cb->style->border_width[0];
                cbw = cb->w - cb->style->border_width[1] - cb->style->border_width[3]; cbh = cb->h - cb->style->border_width[0] - cb->style->border_width[2];
            }
        }
        Edges m, b, p;
        edges(s, cbw, &m, &b, &p);
        int left_set = !is_auto(&s->offset[3]), right_set = !is_auto(&s->offset[1]), top_set = !is_auto(&s->offset[0]), bottom_set = !is_auto(&s->offset[2]);
        int left = left_set ? w_resolve(&s->offset[3], cbw, 0) : 0, right = right_set ? w_resolve(&s->offset[1], cbw, 0) : 0;
        int top = top_set ? w_resolve(&s->offset[0], cbh, 0) : 0, bottom = bottom_set ? w_resolve(&s->offset[2], cbh, 0) : 0;
        int forced_w = -2;
        if (!is_auto(&s->width)) forced_w = -1;
        else if (left_set && right_set) forced_w = cbw - left - right - m.l - m.r;
        int avail = forced_w >= 0 ? forced_w + m.l + m.r : left_set ? cbw - left : right_set ? cbw - right : cbw - (n->static_x - cbx);
        if (avail < 0) avail = 0;
        int x = left_set ? cbx + left : n->static_x;
        int y = top_set ? cby + top : n->static_y;
        int forced_h = -1;
        if (top_set && bottom_set && is_auto(&s->height)) forced_h = cbh - top - bottom - m.t - m.b;
        int saved_base = float_base, saved_count = float_count;
        float_base = float_count;
        int first = doc->item_count;
        int h = layout_box(n, x, y, avail, forced_w, forced_h, cbh);
        float_base = saved_base; float_count = saved_count;
        if (!left_set && right_set) shift_box(n, first, doc->item_count, cbx + cbw - right - m.r - n->w - n->x, 0);
        if (!top_set && bottom_set) shift_box(n, first, doc->item_count, 0, cby + cbh - bottom - m.b - h - n->y);
        if (!left_set && !right_set && s->margin[3].kind == WL_AUTO && s->margin[1].kind == WL_AUTO) { }
        if (fixed) for (int k = first; k < doc->item_count; k++) doc->items[k].fixed = 1;
    }
    deferred_count = 0;
}

void web_layout_tree(WDoc *d)
{
    doc = d;
    viewport_w = d->width; viewport_h = d->viewport_h;
    d->item_count = 0;
    d->layout_pass++;
    float_count = float_base = 0;
    deferred_count = 0;
    current_link = 0;
    marker_for = 0;
    depth_now = 0;
    item_capacity = 32768;
    items = web_alloc_big(sizeof(IItem) * (unsigned)item_capacity);
    if (!items) item_capacity = 0;
    d->items = 0; d->item_capacity = 0;
    item_count = 0;
    WNode *root = d->root;
    if (!root->style) return;
    /* the canvas takes the root's background, or the body's */
    unsigned int canvas = root->style->background;
    if (!(canvas >> 24) && d->body && d->body->style) canvas = d->body->style->background;
    d->canvas_color = (canvas >> 24) ? canvas : 0xffffffffu;
    layout_box(root, 0, 0, d->width, d->width, -1, d->viewport_h);
    layout_deferred();
    int height = 0;
    for (int i = 0; i < d->item_count; i++) { WItem *it = &d->items[i]; if (!it->fixed && it->kind != WI_NONE && it->y + it->h > height && it->kind != WI_CLIP) height = it->y + it->h; }
    if (root->h > height) height = root->h;
    d->height = height;
}

/* Form controls are made once per page, before the first layout, so what
 * is typed survives relayouts. */
void web_prepare_controls(WDoc *d)
{
    doc = d;
    for (WNode *n = d->root; n;) {
        if (n->type == WN_ELEMENT && (n->tag == T_INPUT || n->tag == T_SELECT || n->tag == T_TEXTAREA || n->tag == T_BUTTON)) control_of(n);
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
}
