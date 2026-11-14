/* Gouse Web: HTML5 tokenizer and tree builder. Follows the HTML Living
 * Standard where pages depend on it: character references, raw text
 * (script, style) and RCDATA (title, textarea), void elements, implied
 * html/head/body, and the end tags that HTML lets authors leave out
 * (p, li, dt/dd, option, table rows and cells). Scripts are kept but
 * never run; svg and math content is skipped. */
#include "web.h"

static const char *const tag_names[T_COUNT] = {
    "", "html", "head", "body", "title", "meta", "link", "style", "script", "noscript", "base",
    "div", "span", "p", "a", "b", "strong", "i", "em", "u", "s", "small", "big", "sub", "sup", "code",
    "pre", "blockquote", "br", "hr", "img", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "dl",
    "dt", "dd", "table", "caption", "thead", "tbody", "tfoot", "tr", "td", "th", "col", "colgroup",
    "form", "input", "button", "select", "option", "optgroup", "textarea", "label", "fieldset", "legend",
    "header", "footer", "nav", "main", "section", "article", "aside", "figure", "figcaption", "address",
    "center", "font", "iframe", "svg", "math", "canvas", "video", "audio", "object", "embed", "template",
    "wbr", "area", "param", "source", "track", "details", "summary", "menu", "dir", "picture", "q",
    "nobr", "mark"};
int w_tag_of(const char *name)
{
    for (int i = 1; i < T_COUNT; i++) if (w_eq(tag_names[i], name)) return i;
    return T_UNKNOWN;
}
const char *w_tag_name(int tag) { return tag > 0 && tag < T_COUNT ? tag_names[tag] : ""; }

const char *w_attr(const WNode *n, const char *name)
{
    for (const WAttr *a = n ? n->attrs : 0; a; a = a->next) if (w_eq(a->name, name)) return a->value;
    return 0;
}
int w_has_class(const WNode *n, const char *name)
{
    const char *s = n->class_list;
    int length = w_len(name);
    if (!s || !length) return 0;
    while (*s) {
        while (w_space(*s)) s++;
        const char *start = s;
        while (*s && !w_space(*s)) s++;
        if (s - start == length) {
            int same = 1;
            for (int i = 0; i < length; i++) if (start[i] != name[i]) { same = 0; break; }
            if (same) return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- character references */
static const struct { const char *name; unsigned short code; } entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xa0}, {"copy", 0xa9},
    {"reg", 0xae}, {"trade", 0x2122}, {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xab},
    {"raquo", 0xbb}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201c}, {"rdquo", 0x201d}, {"bdquo", 0x201e},
    {"sbquo", 0x201a}, {"bull", 0x2022}, {"middot", 0xb7}, {"times", 0xd7}, {"divide", 0xf7}, {"deg", 0xb0},
    {"plusmn", 0xb1}, {"sect", 0xa7}, {"para", 0xb6}, {"euro", 0x20ac}, {"pound", 0xa3}, {"yen", 0xa5},
    {"cent", 0xa2}, {"curren", 0xa4}, {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193},
    {"harr", 0x2194}, {"lArr", 0x21d0}, {"rArr", 0x21d2}, {"hearts", 0x2665}, {"spades", 0x2660}, {"clubs", 0x2663},
    {"diams", 0x2666}, {"iexcl", 0xa1}, {"iquest", 0xbf}, {"shy", 0xad}, {"macr", 0xaf}, {"acute", 0xb4},
    {"micro", 0xb5}, {"cedil", 0xb8}, {"ordf", 0xaa}, {"ordm", 0xba}, {"sup1", 0xb9}, {"sup2", 0xb2},
    {"sup3", 0xb3}, {"frac14", 0xbc}, {"frac12", 0xbd}, {"frac34", 0xbe}, {"not", 0xac}, {"brvbar", 0xa6},
    {"uml", 0xa8}, {"Agrave", 0xc0}, {"Aacute", 0xc1}, {"Acirc", 0xc2}, {"Atilde", 0xc3}, {"Auml", 0xc4},
    {"Aring", 0xc5}, {"AElig", 0xc6}, {"Ccedil", 0xc7}, {"Egrave", 0xc8}, {"Eacute", 0xc9}, {"Ecirc", 0xca},
    {"Euml", 0xcb}, {"Igrave", 0xcc}, {"Iacute", 0xcd}, {"Icirc", 0xce}, {"Iuml", 0xcf}, {"ETH", 0xd0},
    {"Ntilde", 0xd1}, {"Ograve", 0xd2}, {"Oacute", 0xd3}, {"Ocirc", 0xd4}, {"Otilde", 0xd5}, {"Ouml", 0xd6},
    {"Oslash", 0xd8}, {"Ugrave", 0xd9}, {"Uacute", 0xda}, {"Ucirc", 0xdb}, {"Uuml", 0xdc}, {"Yacute", 0xdd},
    {"THORN", 0xde}, {"szlig", 0xdf}, {"agrave", 0xe0}, {"aacute", 0xe1}, {"acirc", 0xe2}, {"atilde", 0xe3},
    {"auml", 0xe4}, {"aring", 0xe5}, {"aelig", 0xe6}, {"ccedil", 0xe7}, {"egrave", 0xe8}, {"eacute", 0xe9},
    {"ecirc", 0xea}, {"euml", 0xeb}, {"igrave", 0xec}, {"iacute", 0xed}, {"icirc", 0xee}, {"iuml", 0xef},
    {"eth", 0xf0}, {"ntilde", 0xf1}, {"ograve", 0xf2}, {"oacute", 0xf3}, {"ocirc", 0xf4}, {"otilde", 0xf5},
    {"ouml", 0xf6}, {"oslash", 0xf8}, {"ugrave", 0xf9}, {"uacute", 0xfa}, {"ucirc", 0xfb}, {"uuml", 0xfc},
    {"yacute", 0xfd}, {"thorn", 0xfe}, {"yuml", 0xff}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160},
    {"scaron", 0x161}, {"Yuml", 0x178}, {"fnof", 0x192}, {"circ", 0x2c6}, {"tilde", 0x2dc}, {"ensp", 0x2002},
    {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwnj", 0x200c}, {"zwj", 0x200d}, {"lrm", 0x200e}, {"rlm", 0x200f},
    {"dagger", 0x2020}, {"Dagger", 0x2021}, {"permil", 0x2030}, {"lsaquo", 0x2039}, {"rsaquo", 0x203a},
    {"prime", 0x2032}, {"Prime", 0x2033}, {"oline", 0x203e}, {"minus", 0x2212}, {"le", 0x2264}, {"ge", 0x2265},
    {"ne", 0x2260}, {"asymp", 0x2248}, {"infin", 0x221e}, {"sum", 0x2211}, {"radic", 0x221a}, {"alpha", 0x3b1},
    {"beta", 0x3b2}, {"gamma", 0x3b3}, {"delta", 0x3b4}, {"pi", 0x3c0}, {"mu", 0x3bc}, {"sigma", 0x3c3},
    {"omega", 0x3c9}, {"lambda", 0x3bb}, {"theta", 0x3b8}, {"Delta", 0x394}, {"Omega", 0x3a9}, {"check", 0x2713},
    {"star", 0x2606}, {"numero", 0x2116}, {"loz", 0x25ca}, {"x", 0}};
/* Decodes s[0..length) into out (enough room: never longer). Returns the length. */
static int decode_references(const char *s, int length, char *out, int attribute)
{
    int n = 0;
    for (int i = 0; i < length;) {
        if (s[i] != '&') { out[n++] = s[i++]; continue; }
        int j = i + 1;
        unsigned int code = 0;
        int ok = 0;
        if (j < length && s[j] == '#') {
            j++;
            int hex = j < length && (s[j] == 'x' || s[j] == 'X');
            if (hex) j++;
            int digits = 0;
            while (j < length && digits < 8) {
                int c = s[j], v = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (v < 0) break;
                code = code * (hex ? 16 : 10) + (unsigned int)v; j++; digits++;
            }
            if (digits) {
                ok = 1;
                if (j < length && s[j] == ';') j++;
                if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code < 0xe000)) code = 0xfffd;
                if (code >= 0x80 && code < 0xa0) {   /* windows-1252 numbers, as browsers do */
                    static const unsigned short w1252[32] = {0x20ac,0,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0,0x17d,0,
                                                           0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0,0x17e,0x178};
                    if (w1252[code - 0x80]) code = w1252[code - 0x80];
                }
            }
        } else {
            int k = j;
            while (k < length && k - j < 10 && ((s[k] >= 'a' && s[k] <= 'z') || (s[k] >= 'A' && s[k] <= 'Z') || (s[k] >= '0' && s[k] <= '9'))) k++;
            int size = k - j;
            for (unsigned int e = 0; size && e < sizeof(entities) / sizeof(entities[0]) - 1; e++) {
                int el = w_len(entities[e].name);
                if (el != size) continue;
                int same = 1;
                for (int m = 0; m < el; m++) if (entities[e].name[m] != s[j + m]) { same = 0; break; }
                if (!same) continue;
                /* in attributes, "&amp=" style text without ';' stays as it is */
                if (k < length && s[k] != ';' && attribute && (s[k] == '=' || (s[k] >= 'a' && s[k] <= 'z'))) break;
                code = entities[e].code; ok = 1; j = k;
                if (j < length && s[j] == ';') j++;
                break;
            }
        }
        if (!ok) { out[n++] = s[i++]; continue; }
        n += w_utf8_put(out + n, code);
        i = j;
    }
    return n;
}

/* ---------------------------------------------------------------- tree */
static WDoc *doc;
static WNode *stack[512];
static int depth;
static WNode *new_node(int type)
{
    WNode *n = web_alloc(sizeof(WNode));
    n->type = (unsigned char)type;
    return n;
}
static void append(WNode *parent, WNode *child)
{
    child->parent = parent;
    child->prev = parent->last;
    if (parent->last) parent->last->next = child; else parent->first = child;
    parent->last = child;
}
static WNode *current(void) { return stack[depth - 1]; }
static void push(WNode *n) { if (depth < (int)(sizeof(stack) / sizeof(stack[0]))) stack[depth++] = n; }
static WNode *element(const char *name, int tag)
{
    WNode *n = new_node(WN_ELEMENT);
    n->tag = (unsigned short)tag;
    n->name = tag ? w_tag_name(tag) : web_strdup(name, -1);
    return n;
}
static void ensure_body(void)
{
    if (doc->body) return;
    while (depth > 1) depth--;   /* leave head */
    doc->body = element("body", T_BODY);
    append(doc->root, doc->body);
    push(doc->body);
}
static void add_text(const char *s, int length)
{
    WNode *parent = current();
    if (parent == doc->root || parent == doc->head) {
        int blank = 1;
        for (int i = 0; i < length; i++) if (!w_space(s[i])) { blank = 0; break; }
        if (blank) return;
        ensure_body();
        parent = current();
    }
    char *decoded = web_alloc((unsigned int)length + 1);
    int n = decode_references(s, length, decoded, 0);
    if (parent->last && parent->last->type == WN_TEXT) {   /* merge with the text before */
        WNode *t = parent->last;
        char *joined = web_alloc((unsigned int)(t->length + n) + 1);
        for (int i = 0; i < t->length; i++) joined[i] = t->text[i];
        for (int i = 0; i < n; i++) joined[t->length + i] = decoded[i];
        t->text = joined; t->length += n;
        return;
    }
    WNode *t = new_node(WN_TEXT);
    t->text = decoded; t->length = n;
    append(parent, t);
}
static int in_stack(int tag, int stop_at_table)
{
    for (int i = depth - 1; i > 0; i--) {
        if (stack[i]->tag == tag) return i;
        if (stop_at_table && (stack[i]->tag == T_TABLE || stack[i]->tag == T_TD || stack[i]->tag == T_TH)) return -1;
    }
    return -1;
}
static void pop_to(int index) { if (index > 0) depth = index; }
static void close_if_open(int tag, int stop_at_table)
{
    int i = in_stack(tag, stop_at_table);
    if (i > 0) pop_to(i);
}
static int is_void(int tag)
{
    return tag == T_BR || tag == T_HR || tag == T_IMG || tag == T_INPUT || tag == T_META || tag == T_LINK ||
           tag == T_BASE || tag == T_COL || tag == T_WBR || tag == T_AREA || tag == T_PARAM || tag == T_SOURCE ||
           tag == T_TRACK || tag == T_EMBED;
}
static int closes_p(int tag)
{
    switch (tag) {
    case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER: case T_DETAILS: case T_DIV:
    case T_DL: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_FORM: case T_H1: case T_H2:
    case T_H3: case T_H4: case T_H5: case T_H6: case T_HEADER: case T_HR: case T_MAIN: case T_MENU: case T_NAV:
    case T_OL: case T_P: case T_PRE: case T_SECTION: case T_TABLE: case T_UL: case T_LI: case T_DD: case T_DT:
    case T_DIR: case T_SUMMARY:
        return 1;
    }
    return 0;
}
static int head_content(int tag)
{
    return tag == T_TITLE || tag == T_META || tag == T_LINK || tag == T_STYLE || tag == T_SCRIPT || tag == T_BASE || tag == T_TEMPLATE;
}
static void start_tag(const char *name, WAttr *attrs, int self_closing, const char **raw_end)
{
    int tag = w_tag_of(name);
    *raw_end = 0;
    if (tag == T_HTML) { for (WAttr *a = attrs; a; a = a->next) if (!w_attr(doc->root, a->name)) { WAttr *c = web_alloc(sizeof(WAttr)); *c = *a; c->next = doc->root->attrs; doc->root->attrs = c; } return; }
    if (tag == T_HEAD) return;
    if (tag == T_BODY) {
        ensure_body();
        for (WAttr *a = attrs; a; a = a->next) if (!w_attr(doc->body, a->name)) { WAttr *c = web_alloc(sizeof(WAttr)); *c = *a; c->next = doc->body->attrs; doc->body->attrs = c; }
        return;
    }
    WNode *parent;
    if (!doc->body && (head_content(tag) || tag == T_NOSCRIPT)) parent = current();
    else {
        ensure_body();
        if (closes_p(tag)) close_if_open(T_P, 1);
        if (tag == T_LI) { int i = in_stack(T_LI, 1), list = -1; for (int k = depth - 1; k > 0; k--) if (stack[k]->tag == T_UL || stack[k]->tag == T_OL || stack[k]->tag == T_MENU) { list = k; break; } if (i > list) pop_to(i); }
        if (tag == T_DT || tag == T_DD) { int a = in_stack(T_DT, 1), b = in_stack(T_DD, 1), dl = in_stack(T_DL, 1), i = a > b ? a : b; if (i > dl) pop_to(i); }
        if (tag >= T_H1 && tag <= T_H6 && current()->tag >= T_H1 && current()->tag <= T_H6) depth--;
        if (tag == T_OPTION || tag == T_OPTGROUP) { if (current()->tag == T_OPTION) depth--; if (tag == T_OPTGROUP && current()->tag == T_OPTGROUP) depth--; }
        if (tag == T_A) close_if_open(T_A, 1);
        if (tag == T_NOBR) close_if_open(T_NOBR, 1);
        if (tag == T_TR) { int i = in_stack(T_TR, 0), t = in_stack(T_TABLE, 0); if (i > t) pop_to(i); }
        if (tag == T_TD || tag == T_TH) {
            int td = in_stack(T_TD, 0), th = in_stack(T_TH, 0), t = in_stack(T_TABLE, 0), i = td > th ? td : th;
            if (i > t) pop_to(i);
        }
        if (tag == T_THEAD || tag == T_TBODY || tag == T_TFOOT) {
            int t = in_stack(T_TABLE, 0);
            for (int k = depth - 1; k > t && t > 0; k--)
                if (stack[k]->tag == T_THEAD || stack[k]->tag == T_TBODY || stack[k]->tag == T_TFOOT) { pop_to(k); break; }
        }
        if (tag == T_CAPTION || tag == T_COLGROUP) { int t = in_stack(T_TABLE, 0); if (t > 0) pop_to(t + 1); }
        if (tag == T_TABLE && current()->tag == T_TABLE) depth--;
        /* a cell or row directly in a table, or text-level content after a row */
        parent = current();
    }
    WNode *n = element(name, tag);
    n->attrs = attrs;
    n->id = w_attr(n, "id");
    n->class_list = w_attr(n, "class");
    append(parent, n);
    if (tag == T_BASE && !doc->base[0]) { const char *h = w_attr(n, "href"); if (h) w_resolve_url(doc->url, h, doc->base, sizeof(doc->base)); }
    if (is_void(tag)) return;
    if (tag == T_SCRIPT || tag == T_STYLE || tag == T_TEXTAREA || tag == T_TITLE || tag == T_TEMPLATE ||
        tag == T_SVG || tag == T_MATH || tag == T_IFRAME) { *raw_end = n->name; }
    if (self_closing && (tag == T_SVG || tag == T_MATH || tag == T_UNKNOWN)) { *raw_end = 0; return; }
    push(n);
}
static void end_tag(const char *name)
{
    int tag = w_tag_of(name);
    if (tag == T_HTML || tag == T_BODY || tag == T_HEAD) return;
    if (tag == T_BR) { ensure_body(); append(current(), element("br", T_BR)); return; }
    if (tag == T_P && in_stack(T_P, 1) < 0) { ensure_body(); append(current(), element("p", T_P)); return; }
    for (int i = depth - 1; i > 0; i--) {
        if (tag ? stack[i]->tag == tag : w_eq(stack[i]->name, name)) { depth = i; return; }
        /* a cell or table ends the search for misnested formatting tags */
        if ((stack[i]->tag == T_TABLE || stack[i]->tag == T_TD || stack[i]->tag == T_TH) && tag != T_TABLE && tag != T_TD && tag != T_TH && tag != T_TR &&
            tag != T_TBODY && tag != T_THEAD && tag != T_TFOOT) return;
    }
}

/* ---------------------------------------------------------------- tokenizer */
static int name_char(int c) { return c && !w_space(c) && c != '/' && c != '>' && c != '='; }
WDoc *web_parse(const char *html, int length, const char *url)
{
    doc = web_alloc(sizeof(WDoc));
    w_copy(doc->url, url, sizeof(doc->url));
    doc->zoom = 100;
    doc->focused = -1;
    doc->find_item = -1;
    doc->root = element("html", T_HTML);
    doc->head = element("head", T_HEAD);
    append(doc->root, doc->head);
    depth = 0;
    push(doc->root); push(doc->head);
    const char *s = html, *end = html + length;
    if (length >= 3 && (unsigned char)s[0] == 0xef && (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf) s += 3;
    {   /* a document without <!DOCTYPE html> is in quirks mode */
        const char *p = s;
        while (p < end && (w_space(*p) || (p + 4 < end && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-'))) {
            if (*p == '<') { while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++; p += 3; } else p++;
        }
        doc->quirks = !(p + 9 < end && w_eqn_ci(p, "<!doctype", 9) && w_find_ci(p, "html") && w_find_ci(p, "html") - p < 20);
    }
    while (s < end) {
        const char *lt = s;
        while (lt < end && *lt != '<') lt++;
        if (lt > s) add_text(s, (int)(lt - s));
        if (lt >= end) break;
        s = lt;
        if (s + 1 >= end) { add_text(s, 1); break; }
        if (s[1] == '!') {
            if (s + 3 < end && s[2] == '-' && s[3] == '-') {   /* comment */
                const char *c = s + 4;
                while (c + 2 < end && !(c[0] == '-' && c[1] == '-' && c[2] == '>')) c++;
                s = c + 2 < end ? c + 3 : end;
            } else if (s + 8 < end && w_eqn_ci(s + 2, "[CDATA[", 7)) {
                const char *c = s + 9;
                while (c + 2 < end && !(c[0] == ']' && c[1] == ']' && c[2] == '>')) c++;
                add_text(s + 9, (int)(c - s - 9));
                s = c + 2 < end ? c + 3 : end;
            } else { while (s < end && *s != '>') s++; s++; }
            continue;
        }
        if (s[1] == '?') { while (s < end && *s != '>') s++; s++; continue; }
        int closing = s[1] == '/';
        const char *p = s + 1 + closing;
        if (p >= end || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
            if (closing) { while (s < end && *s != '>') s++; s++; continue; }   /* </ followed by junk: a bogus comment */
            add_text(s, 1); s++; continue;
        }
        char name[64]; int n = 0;
        while (p < end && name_char(*p)) { if (n < 63) name[n++] = (char)w_lower(*p); p++; }
        name[n] = 0;
        WAttr *attrs = 0, *last = 0;
        int self_closing = 0;
        while (p < end && *p != '>') {
            if (w_space(*p)) { p++; continue; }
            if (*p == '/') { self_closing = p + 1 < end && p[1] == '>'; p++; continue; }
            const char *an = p;
            while (p < end && name_char(*p)) p++;
            if (p == an) { p++; continue; }
            int an_length = (int)(p - an);
            while (p < end && w_space(*p)) p++;
            const char *value = ""; int value_length = 0;
            if (p < end && *p == '=') {
                p++;
                while (p < end && w_space(*p)) p++;
                if (p < end && (*p == '"' || *p == '\'')) {
                    char q = *p++;
                    value = p;
                    while (p < end && *p != q) p++;
                    value_length = (int)(p - value);
                    if (p < end) p++;
                } else {
                    value = p;
                    while (p < end && !w_space(*p) && *p != '>') p++;
                    value_length = (int)(p - value);
                }
            }
            if (closing) continue;
            WAttr *a = web_alloc(sizeof(WAttr));
            char *lname = web_alloc((unsigned int)an_length + 1);
            for (int i = 0; i < an_length; i++) lname[i] = (char)w_lower(an[i]);
            a->name = lname;
            char *v = web_alloc((unsigned int)value_length + 1);
            v[decode_references(value, value_length, v, 1)] = 0;
            a->value = v;
            int duplicate = 0;
            for (WAttr *b = attrs; b; b = b->next) if (w_eq(b->name, lname)) duplicate = 1;
            if (duplicate) continue;
            if (last) last->next = a; else attrs = a;
            last = a;
        }
        s = p < end ? p + 1 : end;
        if (closing) { end_tag(name); continue; }
        const char *raw = 0;
        start_tag(name, attrs, self_closing, &raw);
        if (!raw && (w_eq(name, "pre") || w_eq(name, "listing"))) { if (s < end && *s == '\r') s++; if (s < end && *s == '\n') s++; }
        if (raw) {   /* raw text up to the matching end tag */
            int rl = w_len(raw);
            const char *c = s;
            while (c < end) {
                if (c[0] == '<' && c + 1 < end && c[1] == '/' && c + 2 + rl <= end && w_eqn_ci(c + 2, raw, rl) &&
                    (c + 2 + rl == end || c[2 + rl] == '>' || w_space(c[2 + rl]) || c[2 + rl] == '/')) break;
                c++;
            }
            WNode *holder = current();
            int tag = holder->tag;
            if (tag == T_TEXTAREA && s < c && *s == '\n') s++;   /* a newline right after the start tag belongs to the markup */
            if (tag == T_SCRIPT || tag == T_STYLE || tag == T_TEMPLATE || tag == T_SVG || tag == T_MATH || tag == T_IFRAME) {
                if (tag == T_STYLE || tag == T_SCRIPT) {
                    WNode *t = new_node(WN_TEXT);
                    t->text = web_strdup(s, (int)(c - s)); t->length = (int)(c - s);
                    append(holder, t);
                } else if (tag == T_SVG) {
                    /* keep a <title> of an inline icon as its accessible text */
                }
            } else if (c > s) add_text(s, (int)(c - s));   /* title, textarea: references are decoded */
            depth--;
            while (c < end && *c != '>') c++;
            s = c < end ? c + 1 : end;
        }
    }
    if (!doc->body) ensure_body();
    if (!doc->base[0]) w_copy(doc->base, doc->url, sizeof(doc->base));
    for (WNode *n = doc->head->first; n; n = n->next)
        if (n->tag == T_TITLE && n->first) {
            const unsigned char *t = (const unsigned char *)n->first->text;
            int k = 0, space = 0;
            for (int i = 0; i < n->first->length && k < (int)sizeof(doc->title) - 4; i++) {
                if (w_space(t[i])) { space = k > 0; continue; }
                if (space) doc->title[k++] = ' ';
                space = 0;
                doc->title[k++] = (char)t[i];
            }
            doc->title[k] = 0;
            break;
        }
    return doc;
}

const char *web_text_of(const WNode *n, char *out, int capacity)
{
    int k = 0;
    const WNode *stop = n;
    out[0] = 0;
    for (const WNode *c = n->first; c && k < capacity - 1;) {
        if (c->type == WN_TEXT) for (int i = 0; i < c->length && k < capacity - 1; i++) out[k++] = c->text[i];
        if (c->first && !c->pseudo) { c = c->first; continue; }   /* ::before/::after text is not the document's */
        while (c && c != stop && !c->next) c = c->parent;
        if (!c || c == stop) break;
        c = c->next;
    }
    out[k] = 0;
    return out;
}
