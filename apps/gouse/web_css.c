/* Gouse Web: CSS parsing, selector matching and the cascade (CSS Cascade 4,
 * Selectors 4 subset, Media Queries 4 width features, Custom Properties).
 * Every element gets a computed WStyle; ::before and ::after with content
 * become extra child nodes. */
#include "web.h"

/* ---------------------------------------------------------------- properties */
enum {
    PR_DISPLAY, PR_POSITION, PR_FLOAT, PR_CLEAR, PR_VISIBILITY, PR_WHITE_SPACE, PR_TEXT_ALIGN, PR_TEXT_TRANSFORM,
    PR_LIST_STYLE_TYPE, PR_LIST_STYLE_POSITION, PR_BOX_SIZING, PR_OVERFLOW, PR_VERTICAL_ALIGN, PR_FLEX_DIRECTION,
    PR_FLEX_WRAP, PR_JUSTIFY_CONTENT, PR_ALIGN_ITEMS, PR_ALIGN_SELF, PR_ALIGN_CONTENT, PR_TEXT_DECORATION_LINE,
    PR_FONT_STYLE, PR_FONT_WEIGHT, PR_FONT_SIZE, PR_FONT_FAMILY, PR_LINE_HEIGHT, PR_LETTER_SPACING, PR_WORD_SPACING,
    PR_COLOR, PR_BACKGROUND_COLOR, PR_BACKGROUND_IMAGE,
    PR_BORDER_TOP_WIDTH, PR_BORDER_RIGHT_WIDTH, PR_BORDER_BOTTOM_WIDTH, PR_BORDER_LEFT_WIDTH,
    PR_BORDER_TOP_STYLE, PR_BORDER_RIGHT_STYLE, PR_BORDER_BOTTOM_STYLE, PR_BORDER_LEFT_STYLE,
    PR_BORDER_TOP_COLOR, PR_BORDER_RIGHT_COLOR, PR_BORDER_BOTTOM_COLOR, PR_BORDER_LEFT_COLOR,
    PR_BORDER_RADIUS, PR_WIDTH, PR_HEIGHT, PR_MIN_WIDTH, PR_MAX_WIDTH, PR_MIN_HEIGHT, PR_MAX_HEIGHT,
    PR_MARGIN_TOP, PR_MARGIN_RIGHT, PR_MARGIN_BOTTOM, PR_MARGIN_LEFT,
    PR_PADDING_TOP, PR_PADDING_RIGHT, PR_PADDING_BOTTOM, PR_PADDING_LEFT,
    PR_TOP, PR_RIGHT, PR_BOTTOM, PR_LEFT, PR_FLEX_GROW, PR_FLEX_SHRINK, PR_FLEX_BASIS, PR_ORDER,
    PR_ROW_GAP, PR_COLUMN_GAP, PR_Z_INDEX, PR_OPACITY, PR_CONTENT, PR_TEXT_INDENT, PR_BORDER_SPACING,
    PR_BORDER_COLLAPSE, PR_GRID_TEMPLATE_COLUMNS, PR_GRID_COLUMN_START, PR_GRID_COLUMN_END, PR_WORD_BREAK,
    PR_TEXT_DECORATION_COLOR, PR_LIST_STYLE_IMAGE, PR_MASK, PR_CAPTION_SIDE,
    PR_LONGHANDS,
    /* shorthands */
    PR_MARGIN = PR_LONGHANDS, PR_PADDING, PR_BORDER, PR_BORDER_TOP, PR_BORDER_RIGHT, PR_BORDER_BOTTOM, PR_BORDER_LEFT,
    PR_BORDER_WIDTH, PR_BORDER_STYLE, PR_BORDER_COLOR, PR_BACKGROUND, PR_FONT, PR_FLEX, PR_FLEX_FLOW, PR_GAP,
    PR_LIST_STYLE, PR_TEXT_DECORATION, PR_INSET, PR_PLACE_ITEMS, PR_PLACE_CONTENT, PR_GRID_COLUMN, PR_TEXT_WRAP,
    PR_GRID_GAP, PR_OVERFLOW_WRAP, PR_BORDER_BLOCK, PR_BORDER_INLINE, PR_MARGIN_BLOCK, PR_MARGIN_INLINE,
    PR_PADDING_BLOCK, PR_PADDING_INLINE, PR_INSET_INLINE, PR_INSET_BLOCK,
    PR_CUSTOM, PR_COUNT
};
static const struct { const char *name; short id; } property_names[] = {
    {"display", PR_DISPLAY}, {"position", PR_POSITION}, {"float", PR_FLOAT}, {"clear", PR_CLEAR},
    {"visibility", PR_VISIBILITY}, {"white-space", PR_WHITE_SPACE}, {"text-align", PR_TEXT_ALIGN},
    {"text-transform", PR_TEXT_TRANSFORM}, {"list-style-type", PR_LIST_STYLE_TYPE}, {"list-style-position", PR_LIST_STYLE_POSITION},
    {"box-sizing", PR_BOX_SIZING}, {"overflow", PR_OVERFLOW}, {"overflow-x", PR_OVERFLOW}, {"overflow-y", PR_OVERFLOW},
    {"vertical-align", PR_VERTICAL_ALIGN}, {"flex-direction", PR_FLEX_DIRECTION}, {"flex-wrap", PR_FLEX_WRAP},
    {"justify-content", PR_JUSTIFY_CONTENT}, {"align-items", PR_ALIGN_ITEMS}, {"align-self", PR_ALIGN_SELF},
    {"align-content", PR_ALIGN_CONTENT}, {"text-decoration-line", PR_TEXT_DECORATION_LINE}, {"font-style", PR_FONT_STYLE},
    {"font-weight", PR_FONT_WEIGHT}, {"font-size", PR_FONT_SIZE}, {"font-family", PR_FONT_FAMILY}, {"line-height", PR_LINE_HEIGHT},
    {"letter-spacing", PR_LETTER_SPACING}, {"word-spacing", PR_WORD_SPACING}, {"color", PR_COLOR},
    {"background-color", PR_BACKGROUND_COLOR}, {"background-image", PR_BACKGROUND_IMAGE},
    {"border-top-width", PR_BORDER_TOP_WIDTH}, {"border-right-width", PR_BORDER_RIGHT_WIDTH},
    {"border-bottom-width", PR_BORDER_BOTTOM_WIDTH}, {"border-left-width", PR_BORDER_LEFT_WIDTH},
    {"border-top-style", PR_BORDER_TOP_STYLE}, {"border-right-style", PR_BORDER_RIGHT_STYLE},
    {"border-bottom-style", PR_BORDER_BOTTOM_STYLE}, {"border-left-style", PR_BORDER_LEFT_STYLE},
    {"border-top-color", PR_BORDER_TOP_COLOR}, {"border-right-color", PR_BORDER_RIGHT_COLOR},
    {"border-bottom-color", PR_BORDER_BOTTOM_COLOR}, {"border-left-color", PR_BORDER_LEFT_COLOR},
    {"border-radius", PR_BORDER_RADIUS}, {"width", PR_WIDTH}, {"height", PR_HEIGHT}, {"min-width", PR_MIN_WIDTH},
    {"max-width", PR_MAX_WIDTH}, {"min-height", PR_MIN_HEIGHT}, {"max-height", PR_MAX_HEIGHT},
    {"inline-size", PR_WIDTH}, {"block-size", PR_HEIGHT}, {"max-inline-size", PR_MAX_WIDTH}, {"min-inline-size", PR_MIN_WIDTH},
    {"margin-top", PR_MARGIN_TOP}, {"margin-right", PR_MARGIN_RIGHT}, {"margin-bottom", PR_MARGIN_BOTTOM}, {"margin-left", PR_MARGIN_LEFT},
    {"margin-block-start", PR_MARGIN_TOP}, {"margin-block-end", PR_MARGIN_BOTTOM}, {"margin-inline-start", PR_MARGIN_LEFT}, {"margin-inline-end", PR_MARGIN_RIGHT},
    {"padding-top", PR_PADDING_TOP}, {"padding-right", PR_PADDING_RIGHT}, {"padding-bottom", PR_PADDING_BOTTOM}, {"padding-left", PR_PADDING_LEFT},
    {"padding-block-start", PR_PADDING_TOP}, {"padding-block-end", PR_PADDING_BOTTOM}, {"padding-inline-start", PR_PADDING_LEFT}, {"padding-inline-end", PR_PADDING_RIGHT},
    {"top", PR_TOP}, {"right", PR_RIGHT}, {"bottom", PR_BOTTOM}, {"left", PR_LEFT},
    {"flex-grow", PR_FLEX_GROW}, {"flex-shrink", PR_FLEX_SHRINK}, {"flex-basis", PR_FLEX_BASIS}, {"order", PR_ORDER},
    {"row-gap", PR_ROW_GAP}, {"column-gap", PR_COLUMN_GAP}, {"grid-row-gap", PR_ROW_GAP}, {"grid-column-gap", PR_COLUMN_GAP},
    {"z-index", PR_Z_INDEX}, {"opacity", PR_OPACITY}, {"content", PR_CONTENT}, {"text-indent", PR_TEXT_INDENT},
    {"border-spacing", PR_BORDER_SPACING}, {"border-collapse", PR_BORDER_COLLAPSE},
    {"grid-template-columns", PR_GRID_TEMPLATE_COLUMNS}, {"grid-column-start", PR_GRID_COLUMN_START}, {"grid-column-end", PR_GRID_COLUMN_END},
    {"word-break", PR_WORD_BREAK}, {"text-decoration-color", PR_TEXT_DECORATION_COLOR}, {"list-style-image", PR_LIST_STYLE_IMAGE},
    {"margin", PR_MARGIN}, {"padding", PR_PADDING}, {"border", PR_BORDER}, {"border-top", PR_BORDER_TOP},
    {"border-right", PR_BORDER_RIGHT}, {"border-bottom", PR_BORDER_BOTTOM}, {"border-left", PR_BORDER_LEFT},
    {"border-width", PR_BORDER_WIDTH}, {"border-style", PR_BORDER_STYLE}, {"border-color", PR_BORDER_COLOR},
    {"background", PR_BACKGROUND}, {"font", PR_FONT}, {"flex", PR_FLEX}, {"flex-flow", PR_FLEX_FLOW}, {"gap", PR_GAP},
    {"grid-gap", PR_GAP}, {"list-style", PR_LIST_STYLE}, {"text-decoration", PR_TEXT_DECORATION}, {"inset", PR_INSET},
    {"place-items", PR_PLACE_ITEMS}, {"place-content", PR_PLACE_CONTENT}, {"grid-column", PR_GRID_COLUMN},
    {"text-wrap", PR_TEXT_WRAP}, {"text-wrap-mode", PR_TEXT_WRAP}, {"overflow-wrap", PR_OVERFLOW_WRAP}, {"word-wrap", PR_OVERFLOW_WRAP},
    {"border-block", PR_BORDER_BLOCK}, {"border-inline", PR_BORDER_INLINE}, {"margin-block", PR_MARGIN_BLOCK},
    {"margin-inline", PR_MARGIN_INLINE}, {"padding-block", PR_PADDING_BLOCK}, {"padding-inline", PR_PADDING_INLINE},
    {"inset-inline", PR_INSET_INLINE}, {"inset-block", PR_INSET_BLOCK},
    {"border-block-start", PR_BORDER_TOP}, {"border-block-end", PR_BORDER_BOTTOM}, {"border-inline-start", PR_BORDER_LEFT}, {"border-inline-end", PR_BORDER_RIGHT},
    {"inset-inline-start", PR_LEFT}, {"inset-inline-end", PR_RIGHT}, {"inset-block-start", PR_TOP}, {"inset-block-end", PR_BOTTOM},
    {"white-space-collapse", PR_WHITE_SPACE}, {"mask", PR_MASK}, {"mask-image", PR_MASK}, {"-webkit-mask", PR_MASK},
    {"-webkit-mask-image", PR_MASK}, {"caption-side", PR_CAPTION_SIDE},
};
static int property_id(const char *name, int length)
{
    if (length > 2 && name[0] == '-' && name[1] == '-') return PR_CUSTOM;
    for (unsigned int i = 0; i < sizeof(property_names) / sizeof(property_names[0]); i++)
        if (w_len(property_names[i].name) == length && w_eqn_ci(property_names[i].name, name, length)) return property_names[i].id;
    return -1;
}
static int inherited(int p)
{
    switch (p) {
    case PR_VISIBILITY: case PR_WHITE_SPACE: case PR_TEXT_ALIGN: case PR_TEXT_TRANSFORM: case PR_LIST_STYLE_TYPE:
    case PR_LIST_STYLE_POSITION: case PR_FONT_STYLE: case PR_FONT_WEIGHT: case PR_FONT_SIZE: case PR_FONT_FAMILY:
    case PR_LINE_HEIGHT: case PR_LETTER_SPACING: case PR_WORD_SPACING: case PR_COLOR: case PR_TEXT_INDENT:
    case PR_BORDER_SPACING: case PR_BORDER_COLLAPSE: case PR_TEXT_DECORATION_LINE: case PR_WORD_BREAK:
    case PR_TEXT_DECORATION_COLOR: case PR_LIST_STYLE_IMAGE: case PR_CAPTION_SIDE:
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- rules */
typedef struct WMedia { int min_width, max_width, never; } WMedia;
enum { C_ID, C_CLASS, C_ATTR, C_PSEUDO, C_NOT, C_IS, C_NEVER };
enum { PS_FIRST_CHILD, PS_LAST_CHILD, PS_ONLY_CHILD, PS_NTH_CHILD, PS_NTH_LAST_CHILD, PS_FIRST_OF_TYPE, PS_LAST_OF_TYPE,
       PS_NTH_OF_TYPE, PS_ROOT, PS_EMPTY, PS_LINK, PS_CHECKED, PS_DISABLED, PS_ENABLED, PS_ONLY_OF_TYPE, PS_NTH_LAST_OF_TYPE,
       PS_REQUIRED, PS_PLACEHOLDER };
typedef struct WCompound WCompound;
typedef struct WSelector WSelector;
typedef struct {
    unsigned char kind, op, pseudo;
    const char *name, *value;
    int a, b;
    WSelector **list; int list_count;   /* :not(), :is(), :where() */
} WCond;
struct WCompound {
    const char *tag;            /* lowercase, 0 = any */
    WCond *conds; int cond_count;
    char combinator;            /* how this compound relates to prev: ' ' '>' '+' '~' */
    WCompound *prev;
};
struct WSelector { WCompound *last; int specificity; unsigned char pseudo_element; };
typedef struct { short prop; char important; const char *name, *value; } WDecl;
struct WRule {
    WSelector *selector;
    WDecl *decls; int decl_count;
    int order, origin;          /* origin: 0 user agent, 2 author */
    WMedia *media[4]; int media_count;
    int seen;
};

static WDoc *doc;
static int origin;
static WMedia *media_stack[4];
static int media_depth;

static void add_rule(WRule *r)
{
    if (doc->rule_count == doc->rule_capacity) {
        int capacity = doc->rule_capacity ? doc->rule_capacity * 2 : 512;
        WRule **bigger = web_alloc_big(sizeof(WRule *) * (unsigned int)capacity);
        if (!bigger) return;
        for (int i = 0; i < doc->rule_count; i++) bigger[i] = doc->rules[i];
        doc->rules = bigger; doc->rule_capacity = capacity;
    }
    doc->rules[doc->rule_count++] = r;
}

/* ---------------------------------------------------------------- text helpers */
static const char *skip_space(const char *s, const char *end)
{
    for (;;) {
        while (s < end && w_space(*s)) s++;
        if (s + 1 < end && s[0] == '/' && s[1] == '*') {
            s += 2;
            while (s + 1 < end && !(s[0] == '*' && s[1] == '/')) s++;
            s = s + 1 < end ? s + 2 : end;
            continue;
        }
        return s;
    }
}
/* Skips a balanced block or string starting at s. */
static const char *skip_balanced(const char *s, const char *end)
{
    int level = 0;
    while (s < end) {
        char c = *s;
        if (c == '"' || c == '\'') { s++; while (s < end && *s != c) { if (*s == '\\') s++; s++; } s++; continue; }
        if (c == '/' && s + 1 < end && s[1] == '*') { s += 2; while (s + 1 < end && !(s[0] == '*' && s[1] == '/')) s++; s += 2; continue; }
        if (c == '{' || c == '(' || c == '[') level++;
        else if (c == '}' || c == ')' || c == ']') { level--; if (level <= 0) return s + 1; }
        s++;
    }
    return end;
}
/* Copies s..end trimmed, comments removed, CSS escapes kept. */
static char *trimmed(const char *s, const char *end)
{
    while (s < end && w_space(*s)) s++;
    while (end > s && w_space(end[-1])) end--;
    char *out = web_alloc((unsigned int)(end - s) + 1);
    int n = 0;
    while (s < end) {
        if (s + 1 < end && s[0] == '/' && s[1] == '*') {
            s += 2;
            while (s + 1 < end && !(s[0] == '*' && s[1] == '/')) s++;
            s += 2;
            if (n && !w_space(out[n - 1])) out[n++] = ' ';
            continue;
        }
        if (*s == '"' || *s == '\'') {
            char q = *s;
            out[n++] = *s++;
            while (s < end && *s != q) { if (*s == '\\' && s + 1 < end) out[n++] = *s++; out[n++] = *s++; }
            if (s < end) out[n++] = *s++;
            continue;
        }
        out[n++] = *s++;
    }
    while (n && w_space(out[n - 1])) n--;
    out[n] = 0;
    return out;
}
static int ident_char(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 0x80 || c == '\\'; }
/* An identifier with CSS escapes resolved, lowercased when asked. */
static const char *ident(const char **s, int lower)
{
    char buffer[256];
    int n = 0;
    const char *p = *s;
    while (*p && ident_char((unsigned char)*p) && n < 250) {
        if (*p == '\\') {
            p++;
            int code = 0, digits = 0;
            while (digits < 6 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
                code = code * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; digits++;
            }
            if (digits) { if (*p == ' ') p++; n += w_utf8_put(buffer + n, (unsigned)code); }
            else if (*p) buffer[n++] = *p++;
            continue;
        }
        buffer[n++] = lower ? (char)w_lower(*p) : *p;
        p++;
    }
    buffer[n] = 0;
    *s = p;
    return web_strdup(buffer, n);
}

/* ---------------------------------------------------------------- selectors */
static WSelector *parse_selector(const char **s, const char *end, int nested);
static int parse_nth(const char *s, int *a, int *b)
{
    while (w_space(*s)) s++;
    if (w_starts_ci(s, "odd")) { *a = 2; *b = 1; return 1; }
    if (w_starts_ci(s, "even")) { *a = 2; *b = 0; return 1; }
    int sign = 1, va = 0, vb = 0, has_n = 0, digits = 0;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { va = va * 10 + *s++ - '0'; digits++; }
    if (*s == 'n' || *s == 'N') {
        has_n = 1; s++;
        if (!digits) va = 1;
        va *= sign;
        while (w_space(*s)) s++;
        int bs = 1;
        if (*s == '+' || *s == '-') { bs = *s == '-' ? -1 : 1; s++; while (w_space(*s)) s++; while (*s >= '0' && *s <= '9') vb = vb * 10 + *s++ - '0'; vb *= bs; }
        *a = va; *b = vb;
        return 1;
    }
    if (!digits) return 0;
    *a = 0; *b = va * sign;
    (void)has_n;
    return 1;
}
static int compound_specificity(const WCompound *c)
{
    int sp = c->tag ? 1 : 0;
    for (int i = 0; i < c->cond_count; i++) {
        const WCond *k = &c->conds[i];
        if (k->kind == C_ID) sp += 10000;
        else if (k->kind == C_NOT || k->kind == C_IS) {
            int best = 0;
            for (int j = 0; j < k->list_count; j++) if (k->list[j]->specificity > best) best = k->list[j]->specificity;
            if (k->op != 'w') sp += best;   /* :where() adds nothing */
        } else sp += 100;
    }
    return sp;
}
/* One compound selector: tag, #id, .class, [attr], :pseudo. */
static WCompound *parse_compound(const char **s, const char *end, int *pseudo_element)
{
    WCompound *c = web_alloc(sizeof(WCompound));
    WCond conds[16];
    int count = 0;
    const char *p = *s;
    if (p < end && *p == '*') p++;
    else if (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' || *p == '\\' ||
                         (unsigned char)*p >= 0x80 || (*p == '-' && p + 1 < end && ident_char((unsigned char)p[1]))))
        c->tag = ident(&p, 1);
    if (p < end && *p == '|') { p++; if (*p == '*') p++; else c->tag = ident(&p, 1); }   /* namespaces */
    while (p < end && count < 16) {
        WCond k = {0};
        if (*p == '#') { p++; k.kind = C_ID; k.name = ident(&p, 0); }
        else if (*p == '.') { p++; k.kind = C_CLASS; k.name = ident(&p, 0); }
        else if (*p == '[') {
            p++;
            while (w_space(*p)) p++;
            k.kind = C_ATTR; k.name = ident(&p, 1);
            while (w_space(*p)) p++;
            if (*p == '|' && p[1] != '=') { p++; k.name = ident(&p, 1); }
            if (*p == '=' ) { k.op = '='; p++; }
            else if ((*p == '~' || *p == '|' || *p == '^' || *p == '$' || *p == '*') && p[1] == '=') { k.op = (unsigned char)*p; p += 2; }
            if (k.op) {
                while (w_space(*p)) p++;
                if (*p == '"' || *p == '\'') {
                    char q = *p++;
                    const char *v = p;
                    while (p < end && *p != q) { if (*p == '\\') p++; p++; }
                    k.value = web_strdup(v, (int)(p - v));
                    if (p < end) p++;
                } else k.value = ident(&p, 0);
                while (w_space(*p)) p++;
                if (*p == 'i' || *p == 'I' || *p == 's' || *p == 'S') { if ((*p | 32) == 'i') k.pseudo = 1; p++; while (w_space(*p)) p++; }
            }
            while (p < end && *p != ']') p++;
            if (p < end) p++;
        } else if (*p == ':') {
            p++;
            int element = 0;
            if (*p == ':') { element = 1; p++; }
            const char *name = ident(&p, 1);
            const char *args = 0, *args_end = 0;
            if (*p == '(') { args = p + 1; p = skip_balanced(p, end); args_end = p - 1; }
            if (element || w_eq(name, "before") || w_eq(name, "after") || w_eq(name, "first-line") || w_eq(name, "first-letter")) {
                if (w_eq(name, "before")) *pseudo_element = 1;
                else if (w_eq(name, "after")) *pseudo_element = 2;
                else if (w_eq(name, "marker")) *pseudo_element = 3;
                else *pseudo_element = 9;   /* ::placeholder, ::selection...: never */
                continue;
            }
            k.kind = C_PSEUDO;
            if (w_eq(name, "first-child")) k.pseudo = PS_FIRST_CHILD;
            else if (w_eq(name, "last-child")) k.pseudo = PS_LAST_CHILD;
            else if (w_eq(name, "only-child")) k.pseudo = PS_ONLY_CHILD;
            else if (w_eq(name, "first-of-type")) k.pseudo = PS_FIRST_OF_TYPE;
            else if (w_eq(name, "last-of-type")) k.pseudo = PS_LAST_OF_TYPE;
            else if (w_eq(name, "only-of-type")) k.pseudo = PS_ONLY_OF_TYPE;
            else if (w_eq(name, "root")) k.pseudo = PS_ROOT;
            else if (w_eq(name, "empty")) k.pseudo = PS_EMPTY;
            else if (w_eq(name, "link") || w_eq(name, "any-link")) k.pseudo = PS_LINK;
            else if (w_eq(name, "checked")) k.pseudo = PS_CHECKED;
            else if (w_eq(name, "disabled")) k.pseudo = PS_DISABLED;
            else if (w_eq(name, "enabled")) k.pseudo = PS_ENABLED;
            else if (w_eq(name, "required")) k.pseudo = PS_REQUIRED;
            else if (w_eq(name, "placeholder-shown")) k.pseudo = PS_PLACEHOLDER;
            else if ((w_eq(name, "nth-child") || w_eq(name, "nth-last-child") || w_eq(name, "nth-of-type") || w_eq(name, "nth-last-of-type")) && args) {
                k.pseudo = w_eq(name, "nth-child") ? PS_NTH_CHILD : w_eq(name, "nth-last-child") ? PS_NTH_LAST_CHILD :
                           w_eq(name, "nth-of-type") ? PS_NTH_OF_TYPE : PS_NTH_LAST_OF_TYPE;
                char text[64]; int n = (int)(args_end - args); if (n > 63) n = 63;
                for (int i = 0; i < n; i++) text[i] = args[i];
                text[n] = 0;
                if (!parse_nth(text, &k.a, &k.b)) k.kind = C_NEVER;
            } else if ((w_eq(name, "not") || w_eq(name, "is") || w_eq(name, "where") || w_eq(name, "matches") || w_eq(name, "-webkit-any")) && args) {
                k.kind = w_eq(name, "not") ? C_NOT : C_IS;
                k.op = w_eq(name, "where") ? 'w' : 0;
                WSelector *list[16]; int n = 0;
                const char *q = args;
                while (q < args_end && n < 16) {
                    q = skip_space(q, args_end);
                    WSelector *sel = parse_selector(&q, args_end, 1);
                    if (sel) list[n++] = sel;
                    q = skip_space(q, args_end);
                    if (q < args_end && *q == ',') q++;
                    else break;
                }
                k.list = web_alloc(sizeof(WSelector *) * (unsigned)(n ? n : 1));
                for (int i = 0; i < n; i++) k.list[i] = list[i];
                k.list_count = n;
            } else if (w_eq(name, "lang") || w_eq(name, "dir") || w_eq(name, "defined") || w_eq(name, "scope") ||
                       w_eq(name, "valid") || w_eq(name, "optional") || w_eq(name, "read-write")) continue;   /* always true here */
            else k.kind = C_NEVER;   /* :hover, :focus, :visited, :has()...: a page that was never touched */
        } else break;
        conds[count++] = k;
    }
    c->conds = web_alloc(sizeof(WCond) * (unsigned)(count ? count : 1));
    for (int i = 0; i < count; i++) c->conds[i] = conds[i];
    c->cond_count = count;
    *s = p;
    return c;
}
static WSelector *parse_selector(const char **s, const char *end, int nested)
{
    const char *p = skip_space(*s, end);
    WCompound *last = 0;
    char combinator = 0;
    int specificity = 0, pseudo = 0;
    while (p < end && *p != ',' && *p != '{' && !(nested && *p == ')')) {
        if (*p == '>' || *p == '+' || *p == '~') { combinator = *p++; p = skip_space(p, end); continue; }
        const char *before = p;
        WCompound *c = parse_compound(&p, end, &pseudo);
        if (p == before) {   /* not understood: drop this selector */
            while (p < end && *p != ',' && *p != '{' && !(nested && *p == ')')) { if (*p == '(' || *p == '[') { p = skip_balanced(p, end); continue; } p++; }
            *s = p;
            return 0;
        }
        c->prev = last;
        c->combinator = last ? (combinator ? combinator : ' ') : 0;
        specificity += compound_specificity(c);
        last = c;
        combinator = 0;
        const char *q = p;
        p = skip_space(p, end);
        if (p > q && p < end && *p != ',' && *p != '{' && *p != '>' && *p != '+' && *p != '~' && !(nested && *p == ')')) combinator = ' ';
    }
    *s = p;
    if (!last || pseudo == 9) return 0;
    WSelector *sel = web_alloc(sizeof(WSelector));
    sel->last = last; sel->specificity = specificity; sel->pseudo_element = (unsigned char)pseudo;
    return sel;
}

/* ---------------------------------------------------------------- matching */
static int element_index(const WNode *n, int from_end, int same_type)
{
    int index = 1;
    for (const WNode *s = from_end ? n->next : n->prev; s; s = from_end ? s->next : s->prev)
        if (s->type == WN_ELEMENT && !s->pseudo && (!same_type || w_eq(s->name, n->name))) index++;
    return index;
}
static int nth(int a, int b, int index)
{
    if (!a) return index == b;
    int k = index - b;
    return k % a == 0 && k / a >= 0;
}
static int match_selector(const WNode *n, const WSelector *sel);
static int match_attr(const WNode *n, const WCond *k)
{
    const char *v = w_attr(n, k->name);
    if (!v) return 0;
    if (!k->op) return 1;
    const char *want = k->value ? k->value : "";
    int wl = w_len(want), vl = w_len(v);
    int (*eqn)(const char *, const char *, int) = w_eqn_ci;
    if (!k->pseudo) {   /* case-sensitive unless [... i] (type, lang and friends are not) */
        int sensitive = !(w_eq(k->name, "type") || w_eq(k->name, "lang") || w_eq(k->name, "dir") || w_eq(k->name, "rel"));
        if (sensitive) {
            switch (k->op) {
            case '=': return w_eq(v, want);
            case '^': for (int i = 0; i < wl; i++) if (v[i] != want[i]) return 0; return wl > 0;
            case '$': if (wl > vl || !wl) return 0; for (int i = 0; i < wl; i++) if (v[vl - wl + i] != want[i]) return 0; return 1;
            case '*': if (!wl) return 0; for (int i = 0; i + wl <= vl; i++) { int ok = 1; for (int j = 0; j < wl; j++) if (v[i + j] != want[j]) { ok = 0; break; } if (ok) return 1; } return 0;
            case '~': { const char *p = v; while (*p) { while (w_space(*p)) p++; const char *st = p; while (*p && !w_space(*p)) p++; if (p - st == wl && wl) { int ok = 1; for (int j = 0; j < wl; j++) if (st[j] != want[j]) { ok = 0; break; } if (ok) return 1; } } return 0; }
            case '|': if (w_eq(v, want)) return 1; for (int i = 0; i < wl; i++) if (v[i] != want[i]) return 0; return v[wl] == '-';
            }
        }
    }
    switch (k->op) {
    case '=': return w_eq_ci(v, want);
    case '^': return wl && eqn(v, want, wl);
    case '$': return wl && wl <= vl && eqn(v + vl - wl, want, wl);
    case '*': return wl && w_find_ci(v, want) != 0;
    case '~': { const char *p = v; while (*p) { while (w_space(*p)) p++; const char *st = p; while (*p && !w_space(*p)) p++; if (p - st == wl && wl && eqn(st, want, wl)) return 1; } return 0; }
    case '|': return w_eq_ci(v, want) || (eqn(v, want, wl) && v[wl] == '-');
    }
    return 0;
}
static int match_compound(const WNode *n, const WCompound *c)
{
    if (n->type != WN_ELEMENT) return 0;
    if (c->tag && !w_eq(c->tag, n->name)) return 0;
    for (int i = 0; i < c->cond_count; i++) {
        const WCond *k = &c->conds[i];
        switch (k->kind) {
        case C_ID: if (!n->id || !w_eq(n->id, k->name)) return 0; break;
        case C_CLASS: if (!w_has_class(n, k->name)) return 0; break;
        case C_ATTR: if (!match_attr(n, k)) return 0; break;
        case C_NEVER: return 0;
        case C_NOT: for (int j = 0; j < k->list_count; j++) if (match_selector(n, k->list[j])) return 0; break;
        case C_IS: { int any = 0; for (int j = 0; j < k->list_count && !any; j++) any = match_selector(n, k->list[j]); if (!any) return 0; break; }
        case C_PSEUDO:
            switch (k->pseudo) {
            case PS_FIRST_CHILD: if (element_index(n, 0, 0) != 1) return 0; break;
            case PS_LAST_CHILD: if (element_index(n, 1, 0) != 1) return 0; break;
            case PS_ONLY_CHILD: if (element_index(n, 0, 0) != 1 || element_index(n, 1, 0) != 1) return 0; break;
            case PS_FIRST_OF_TYPE: if (element_index(n, 0, 1) != 1) return 0; break;
            case PS_LAST_OF_TYPE: if (element_index(n, 1, 1) != 1) return 0; break;
            case PS_ONLY_OF_TYPE: if (element_index(n, 0, 1) != 1 || element_index(n, 1, 1) != 1) return 0; break;
            case PS_NTH_CHILD: if (!nth(k->a, k->b, element_index(n, 0, 0))) return 0; break;
            case PS_NTH_LAST_CHILD: if (!nth(k->a, k->b, element_index(n, 1, 0))) return 0; break;
            case PS_NTH_OF_TYPE: if (!nth(k->a, k->b, element_index(n, 0, 1))) return 0; break;
            case PS_NTH_LAST_OF_TYPE: if (!nth(k->a, k->b, element_index(n, 1, 1))) return 0; break;
            case PS_ROOT: if (n->parent) return 0; break;
            case PS_EMPTY: for (const WNode *c2 = n->first; c2; c2 = c2->next) if (c2->type == WN_ELEMENT || (c2->type == WN_TEXT && c2->length)) return 0; break;
            case PS_LINK: if ((n->tag != T_A && n->tag != T_AREA) || !w_attr(n, "href")) return 0; break;
            case PS_CHECKED: if (n->control ? !n->control->checked && !(n->tag == T_OPTION) : !w_attr(n, "checked") && !w_attr(n, "selected")) return 0; break;
            case PS_DISABLED: if (!w_attr(n, "disabled")) return 0; break;
            case PS_ENABLED: if (w_attr(n, "disabled") || (n->tag != T_INPUT && n->tag != T_BUTTON && n->tag != T_SELECT && n->tag != T_TEXTAREA)) return 0; break;
            case PS_REQUIRED: if (!w_attr(n, "required")) return 0; break;
            case PS_PLACEHOLDER: if (!w_attr(n, "placeholder") || (n->control && n->control->value && n->control->value[0])) return 0; break;
            }
            break;
        }
    }
    return 1;
}
static int match_from(const WNode *n, const WCompound *c)
{
    if (!match_compound(n, c)) return 0;
    if (!c->prev) return 1;
    switch (c->combinator) {
    case '>': return n->parent && match_from(n->parent, c->prev);
    case ' ': for (const WNode *a = n->parent; a; a = a->parent) if (match_from(a, c->prev)) return 1; return 0;
    case '+': { const WNode *s = n->prev; while (s && s->type != WN_ELEMENT) s = s->prev; return s && match_from(s, c->prev); }
    case '~': for (const WNode *s = n->prev; s; s = s->prev) if (s->type == WN_ELEMENT && match_from(s, c->prev)) return 1; return 0;
    }
    return 0;
}
static int match_selector(const WNode *n, const WSelector *sel) { return match_from(n, sel->last); }

/* The parsed selectors live above the arena's top only while matching, so
 * a laid-out page keeps its layout memory. */
int web_query(WDoc *d, WNode *scope, const char *selector, WNode **out, int max)
{
    WSelector *list[32];
    int count = 0, found = 0;
    unsigned int mark = web_mark();
    const char *p = selector, *end = selector + w_len(selector);
    for (;;) {
        WSelector *s = count < 32 ? parse_selector(&p, end, 0) : 0;
        if (!s) { web_release(mark); return -1; }
        list[count++] = s;
        p = skip_space(p, end);
        if (p >= end) break;
        if (*p != ',') { web_release(mark); return -1; }
        p++;
    }
    WDoc *saved = w_doc;
    w_doc = d;
    for (WNode *n = scope->first; n && found < max;) {
        if (n->type == WN_ELEMENT && !n->pseudo)
            for (int i = 0; i < count; i++) if (match_selector(n, list[i])) { out[found++] = n; break; }
        if (n->first && !n->pseudo) { n = n->first; continue; }
        while (n && n != scope && !n->next) n = n->parent;
        n = n && n != scope ? n->next : 0;
    }
    w_doc = saved;
    web_release(mark);
    return found;
}

/* ---------------------------------------------------------------- index */
#define BUCKETS 2048
typedef struct Entry { const char *key; WRule *rule; struct Entry *next; } Entry;
typedef struct { Entry *bucket[BUCKETS]; Entry *universal; } Index;
static unsigned int hash(const char *s, char kind)
{
    unsigned int h = 2166136261u ^ (unsigned char)kind;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h & (BUCKETS - 1);
}
static void index_rule(WRule *r)
{
    Index *ix = doc->index;
    Entry *e = web_alloc(sizeof(Entry));
    e->rule = r;
    const WCompound *c = r->selector->last;
    const char *key = 0; char kind = 0;
    for (int i = 0; i < c->cond_count && !key; i++) if (c->conds[i].kind == C_ID) { key = c->conds[i].name; kind = '#'; }
    for (int i = 0; i < c->cond_count && !key; i++) if (c->conds[i].kind == C_CLASS) { key = c->conds[i].name; kind = '.'; }
    if (!key && c->tag) { key = c->tag; kind = 't'; }
    if (!key) { e->next = ix->universal; ix->universal = e; return; }
    char *k = web_alloc((unsigned int)w_len(key) + 2);
    k[0] = kind; w_copy(k + 1, key, w_len(key) + 1);
    e->key = k;
    unsigned int h = hash(key, kind);
    e->next = ix->bucket[h]; ix->bucket[h] = e;
}

/* ---------------------------------------------------------------- stylesheet parsing */
static WDecl decl_buffer[512];
static int parse_declarations(const char *s, const char *end, WDecl *out, int max)
{
    int count = 0;
    while (s < end && count < max) {
        s = skip_space(s, end);
        if (s >= end) break;
        if (*s == ';') { s++; continue; }
        const char *name = s;
        while (s < end && *s != ':' && *s != ';' && *s != '{' && *s != '}') s++;
        if (s >= end || *s != ':') {   /* nested rule or junk: skip it */
            if (s < end && *s == '{') s = skip_balanced(s, end);
            else if (s < end) s++;
            continue;
        }
        const char *name_end = s;
        while (name_end > name && w_space(name_end[-1])) name_end--;
        s++;
        const char *value = s;
        while (s < end && *s != ';' && *s != '}') {
            if (*s == '(' || *s == '[' || *s == '{') { s = skip_balanced(s, end); continue; }
            if (*s == '"' || *s == '\'') { char q = *s++; while (s < end && *s != q) { if (*s == '\\') s++; s++; } s++; continue; }
            s++;
        }
        const char *value_end = s;
        if (s < end) s++;
        int prop = property_id(name, (int)(name_end - name));
        if (prop < 0) continue;
        char *v = trimmed(value, value_end);
        int important = 0, n = w_len(v);
        for (int i = n - 1; i > 0; i--) {
            if (v[i] == '!') {
                const char *t = v + i + 1;
                while (w_space(*t)) t++;
                if (w_starts_ci(t, "important")) { important = 1; v[i] = 0; while (i > 0 && w_space(v[i - 1])) v[--i] = 0; }
                break;
            }
            if (!((v[i] >= 'a' && v[i] <= 'z') || (v[i] >= 'A' && v[i] <= 'Z') || w_space(v[i]))) break;
        }
        if (!v[0] && prop != PR_CUSTOM) continue;
        out[count].prop = (short)prop;
        out[count].important = (char)important;
        out[count].name = prop == PR_CUSTOM ? web_strdup(name, (int)(name_end - name)) : 0;
        out[count].value = v;
        count++;
    }
    return count;
}
/* Media queries: width features of screens; print and dark modes are off. */
static WMedia *parse_media(const char *s, const char *end)
{
    WMedia *m = web_alloc(sizeof(WMedia));
    m->min_width = -1; m->max_width = -1;
    char *text = trimmed(s, end);
    if (!text[0]) return 0;
    /* a list: true when any query is; approximate with the first that is not
     * "print" */
    const char *q = text;
    int never_all = 1;
    while (*q) {
        const char *qe = q;
        int level = 0;
        while (*qe && (level || *qe != ',')) { if (*qe == '(') level++; if (*qe == ')') level--; qe++; }
        int never = 0, negate = 0;
        int min_w = -1, max_w = -1;
        const char *p = q;
        while (p < qe && w_space(*p)) p++;
        if (w_starts_ci(p, "not ")) { negate = 1; p += 4; }
        if (w_starts_ci(p, "only ")) p += 5;
        if (w_starts_ci(p, "print") || w_starts_ci(p, "speech") || w_starts_ci(p, "tv")) never = 1;
        for (const char *f = p; f < qe; f++) {
            if (*f != '(') continue;
            const char *fe = f;
            while (fe < qe && *fe != ')') fe++;
            const char *g = f + 1;
            while (w_space(*g)) g++;
            int value_ok = 0;
            const char *colon = g;
            while (colon < fe && *colon != ':' && *colon != '<' && *colon != '>') colon++;
            WLen l = w_parse_len(colon < fe ? colon + 1 + (colon[1] == '=') : g, 16, 16, &value_ok);
            int px = l.px;
            if (w_starts_ci(g, "min-width")) { if (value_ok) min_w = px; }
            else if (w_starts_ci(g, "max-width")) { if (value_ok) max_w = px; }
            else if (w_starts_ci(g, "width")) {   /* range syntax: width >= 600px, width < 800px */
                int op_ge = colon < fe && *colon == '>', strict = colon < fe && colon[1] != '=';
                if (value_ok && colon < fe && *colon != ':') { if (op_ge) min_w = px + strict; else max_w = px - strict; }
            }
            else if (w_starts_ci(g, "prefers-color-scheme")) { if (w_find_ci(g, "dark") && w_find_ci(g, "dark") < fe) never = 1; }
            else if (w_starts_ci(g, "orientation")) { if (w_find_ci(g, "portrait") && w_find_ci(g, "portrait") < fe) never = 1; }
            else if (w_starts_ci(g, "hover") || w_starts_ci(g, "pointer") || w_starts_ci(g, "any-hover") || w_starts_ci(g, "any-pointer")) {
                if ((w_find_ci(g, "none") && w_find_ci(g, "none") < fe) || (w_find_ci(g, "coarse") && w_find_ci(g, "coarse") < fe)) never = 1;
            }
            else if (w_starts_ci(g, "prefers-reduced-motion") || w_starts_ci(g, "min-height") || w_starts_ci(g, "max-height") ||
                     w_starts_ci(g, "color") || w_starts_ci(g, "min-aspect-ratio") || w_starts_ci(g, "update") || w_starts_ci(g, "scripting")) { }
            else if (w_starts_ci(g, "-webkit-min-device-pixel-ratio") || w_starts_ci(g, "min-resolution") || w_starts_ci(g, "forced-colors") ||
                     w_starts_ci(g, "prefers-contrast") || w_starts_ci(g, "-ms-high-contrast") || w_starts_ci(g, "display-mode")) never = 1;
            f = fe;
        }
        if (negate) never = !never && min_w < 0 && max_w < 0 ? 1 : 0;   /* "not print" and friends */
        if (!never) {
            never_all = 0;
            if (m->min_width < 0 && m->max_width < 0 && (min_w >= 0 || max_w >= 0)) { m->min_width = min_w; m->max_width = max_w; }
            else if (min_w < 0 && max_w < 0) { m->min_width = m->max_width = -1; break; }   /* an unconditional query */
        }
        q = *qe ? qe + 1 : qe;
    }
    m->never = never_all;
    return m;
}
static int media_ok(const WRule *r, int width)
{
    for (int i = 0; i < r->media_count; i++) {
        const WMedia *m = r->media[i];
        if (!m) continue;
        if (m->never) return 0;
        if (m->min_width >= 0 && width < m->min_width) return 0;
        if (m->max_width >= 0 && width > m->max_width) return 0;
    }
    return 1;
}
static void parse_rules(const char *s, const char *end, const char *base, int depth);
static void at_rule(const char **sp, const char *end, const char *base, int depth)
{
    const char *s = *sp + 1;
    const char *name = ident(&s, 1);
    const char *prelude = s;
    while (s < end && *s != '{' && *s != ';') { if (*s == '(' ) { s = skip_balanced(s, end); continue; } if (*s == '"' || *s == '\'') { char q = *s++; while (s < end && *s != q) s++; } s++; }
    const char *prelude_end = s;
    if (s >= end || *s == ';') {
        if (w_eq(name, "import") && depth < 3) {
            char url[512]; int n = 0;
            const char *p = skip_space(prelude, prelude_end);
            if (w_starts_ci(p, "url(")) p += 4;
            while (p < prelude_end && (*p == '"' || *p == '\'' || w_space(*p))) p++;
            while (p < prelude_end && *p != '"' && *p != '\'' && *p != ')' && !w_space(*p) && n < 511) url[n++] = *p++;
            url[n] = 0;
            while (p < prelude_end && *p != ' ') p++;
            WMedia *m = parse_media(p, prelude_end);
            if (n && (!m || !m->never)) {
                char full[512];
                w_resolve_url(base, url, full, sizeof(full));
                WebResource r;
                web_progress("Loading style sheet...");
                if (!web_fetch(full, &r, 1 << 20, 0, 0) && r.data) web_add_stylesheet(doc, r.data, r.length, r.url[0] ? r.url : full, depth + 1);
            }
        }
        *sp = s < end ? s + 1 : end;
        return;
    }
    const char *block_end = skip_balanced(s, end);
    const char *inner = s + 1, *inner_end = block_end - 1;
    if (inner_end < inner) inner_end = inner;
    if (w_eq(name, "media") || w_eq(name, "container")) {
        WMedia *m = w_eq(name, "media") ? parse_media(prelude, prelude_end) : 0;
        if (!m || !m->never) {
            if (media_depth < 4) media_stack[media_depth++] = m;
            parse_rules(inner, inner_end, base, depth);
            if (media_depth > 0) media_depth--;
        }
    } else if (w_eq(name, "supports")) {
        const char *p = skip_space(prelude, prelude_end);
        int negative = w_starts_ci(p, "not");
        if (w_find_ci(p, "selector(") && w_find_ci(p, ":has")) negative = 1;
        if (!negative) parse_rules(inner, inner_end, base, depth);
    } else if (w_eq(name, "layer") || w_eq(name, "scope"))
        parse_rules(inner, inner_end, base, depth);
    *sp = block_end;
}
static void parse_rules(const char *s, const char *end, const char *base, int depth)
{
    while (s < end) {
        s = skip_space(s, end);
        if (s >= end) break;
        if (*s == '<' || *s == '-') {   /* <!-- and --> around old style sheets */
            if (s + 3 < end && s[0] == '<' && s[1] == '!' && s[2] == '-' && s[3] == '-') { s += 4; continue; }
            if (s + 2 < end && s[0] == '-' && s[1] == '-' && s[2] == '>') { s += 3; continue; }
        }
        if (*s == '@') { at_rule(&s, end, base, depth); continue; }
        if (*s == '}') { s++; continue; }
        const char *prelude = s;
        while (s < end && *s != '{') { if (*s == '"' || *s == '\'') { char q = *s++; while (s < end && *s != q) s++; } s++; }
        if (s >= end) break;
        const char *block_end = skip_balanced(s, end);
        int count = parse_declarations(s + 1, block_end - 1, decl_buffer, 512);
        if (count) {
            WDecl *decls = web_alloc(sizeof(WDecl) * (unsigned)count);
            for (int i = 0; i < count; i++) decls[i] = decl_buffer[i];
            const char *p = prelude;
            while (p < s) {
                WSelector *sel = parse_selector(&p, s, 0);
                if (sel) {
                    WRule *r = web_alloc(sizeof(WRule));
                    r->selector = sel; r->decls = decls; r->decl_count = count;
                    r->order = doc->rule_order++; r->origin = origin;
                    for (int i = 0; i < media_depth; i++) r->media[r->media_count++] = media_stack[i];
                    add_rule(r);
                    index_rule(r);
                }
                while (p < s && *p != ',') { if (*p == '(') { p = skip_balanced(p, s); continue; } p++; }
                if (p < s) p++;
            }
        }
        s = block_end;
    }
}
void web_add_stylesheet(WDoc *d, const char *css, int length, const char *base, int depth)
{
    doc = d;
    if (!doc->index) doc->index = web_alloc_big(sizeof(Index));
    if (!doc->index) return;
    int saved = media_depth;
    media_depth = 0;
    parse_rules(css, css + length, base, depth);
    media_depth = saved;
}

/* ---------------------------------------------------------------- user agent style sheet */
static const char ua_css[] =
    "html,address,blockquote,body,center,dd,details,dialog,dir,div,dl,dt,fieldset,figcaption,figure,footer,form,h1,h2,h3,h4,h5,h6,"
    "header,hgroup,hr,legend,listing,main,menu,nav,ol,p,plaintext,pre,search,section,summary,ul,xmp,article,aside,optgroup,frameset,frame{display:block}"
    "head,link,meta,script,style,title,template,noembed,noframes,param,rp,base,datalist,area,[hidden],dialog:not([open]),input[type=hidden],source,track,map{display:none}"
    "li{display:list-item}table{display:table;border-spacing:2px;border-collapse:separate;box-sizing:border-box}"
    "caption{display:table-caption;text-align:center}thead{display:table-header-group;vertical-align:middle}"
    "tbody{display:table-row-group;vertical-align:middle}tfoot{display:table-footer-group;vertical-align:middle}"
    "tr{display:table-row;vertical-align:inherit}td,th{display:table-cell;vertical-align:inherit;padding:1px}"
    "th{font-weight:bold;text-align:center}col{display:table-column}colgroup{display:table-column}"
    "body{margin:8px}h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}h4{margin:1.33em 0;font-weight:bold}h5{font-size:.83em;margin:1.67em 0;font-weight:bold}"
    "h6{font-size:.67em;margin:2.33em 0;font-weight:bold}p,dl,multicol{margin:1em 0}blockquote,figure{margin:1em 40px}"
    "ul,ol,menu,dir{margin:1em 0;padding-left:40px}ul,menu,dir{list-style-type:disc}ol{list-style-type:decimal}"
    "ul ul,ol ul,menu ul{list-style-type:circle}ul ul ul,ul ol ul,ol ul ul{list-style-type:square}"
    "ul ul,ul ol,ol ul,ol ol,ul menu,ol menu{margin-top:0;margin-bottom:0}dd{margin-left:40px}"
    "b,strong{font-weight:bolder}i,em,cite,var,dfn,address{font-style:italic}u,ins,abbr[title]{text-decoration:underline}"
    "s,strike,del{text-decoration:line-through}pre,code,kbd,samp,tt,listing,xmp,plaintext{font-family:monospace}"
    "code,kbd,samp,tt{font-size:.875em}pre,xmp,listing,plaintext{white-space:pre;margin:1em 0;font-size:.875em}"
    "small{font-size:smaller}big{font-size:larger}sub{vertical-align:sub;font-size:smaller}sup{vertical-align:super;font-size:smaller}"
    "a:link{color:#0000ee;text-decoration:underline}mark{background-color:yellow;color:black}"
    "hr{border-style:inset;border-width:1px;color:gray;border-color:#888;margin:.5em auto}"
    "center{text-align:-webkit-center}nobr{white-space:nowrap}"
    "input,select,textarea,button{display:inline-block;font-size:13px;font-family:sans-serif;font-weight:normal;font-style:normal;text-indent:0;line-height:normal;color:black;text-decoration:none;text-transform:none;letter-spacing:normal}"
    "textarea{font-family:monospace;white-space:pre-wrap}"
    "input,textarea,select{border:1px solid #767676;padding:2px 4px;background-color:white;border-radius:2px}"
    "button,input[type=submit],input[type=button],input[type=reset]{border:1px solid #767676;border-radius:3px;background-color:#efefef;padding:2px 8px;text-align:center}"
    "input[type=checkbox],input[type=radio]{border:0;padding:0;margin:3px 3px 3px 4px;background-color:transparent}"
    "input[type=image]{border:0;padding:0;background-color:transparent}select{padding-right:20px}"
    "fieldset{margin:0 2px;padding:.35em .75em .625em;border:2px groove #c0c0c0}legend{padding:0 2px}"
    "q::before{content:open-quote}q::after{content:close-quote}summary{display:block}"
    "details:not([open])>:not(summary){display:none}img,svg,video,canvas,iframe,embed,object{display:inline-block}iframe{border:2px inset #ccc}"
    "ruby{display:inline}rt{font-size:50%}select,option,optgroup{white-space:nowrap}option,optgroup{display:none}"
    "noscript{display:none}"
    "video,audio{display:inline-block}audio:not([controls]){display:none}";

/* ---------------------------------------------------------------- the cascade */
static const char *spec[PR_COUNT];
static WCustom *customs;
typedef struct { WRule *rule; } Match;
static Match matches[1024];
static int match_count, pass;

static void collect_from(Entry *e, const WNode *n, char kind, const char *key, int pseudo)
{
    for (; e; e = e->next) {
        if (e->rule->seen == pass) continue;
        if (key && (e->key[0] != kind || !w_eq(e->key + 1, key))) continue;
        if (e->rule->selector->pseudo_element != pseudo) continue;
        if (!media_ok(e->rule, doc->width)) continue;
        if (!match_selector(n, e->rule->selector)) continue;
        e->rule->seen = pass;
        if (match_count < 1024) matches[match_count++].rule = e->rule;
    }
}
static int before(const WRule *a, const WRule *b)
{
    if (a->origin != b->origin) return a->origin < b->origin;
    if (a->selector->specificity != b->selector->specificity) return a->selector->specificity < b->selector->specificity;
    return a->order < b->order;
}
static void collect(const WNode *n, int pseudo)
{
    Index *ix = doc->index;
    match_count = 0;
    pass++;
    if (!ix) return;
    if (n->id) collect_from(ix->bucket[hash(n->id, '#')], n, '#', n->id, pseudo);
    const char *s = n->class_list;
    while (s && *s) {
        while (w_space(*s)) s++;
        char name[128]; int k = 0;
        while (*s && !w_space(*s)) { if (k < 127) name[k++] = *s; s++; }
        name[k] = 0;
        if (k) collect_from(ix->bucket[hash(name, '.')], n, '.', name, pseudo);
    }
    collect_from(ix->bucket[hash(n->name, 't')], n, 't', n->name, pseudo);
    collect_from(ix->universal, n, 0, 0, pseudo);
    for (int i = 1; i < match_count; i++) {
        Match m = matches[i]; int j = i;
        while (j > 0 && before(m.rule, matches[j - 1].rule)) { matches[j] = matches[j - 1]; j--; }
        matches[j] = m;
    }
}
/* var() substitution with the custom properties in scope. */
static const char *custom_value(const char *name, int length)
{
    for (WCustom *c = customs; c; c = c->next)
        if (w_len(c->name) == length && w_eqn_ci(c->name, name, length)) return c->value;
    return 0;
}
static const char *substitute(const char *v, int level)
{
    if (!w_find_ci(v, "var(") || level > 8) return v;
    char out[2048]; int n = 0;
    const char *p = v;
    while (*p && n < 2000) {
        if (!w_starts_ci(p, "var(")) { out[n++] = *p++; continue; }
        const char *start = p + 4, *close = skip_balanced(p + 3, p + w_len(p));
        const char *q = start;
        while (w_space(*q)) q++;
        const char *name = q;
        while (*q && *q != ',' && *q != ')' && !w_space(*q)) q++;
        const char *value = custom_value(name, (int)(q - name));
        while (w_space(*q)) q++;
        if (!value && *q == ',') {   /* fallback */
            char fallback[1024]; int k = 0;
            q++;
            while (q < close - 1 && k < 1023) fallback[k++] = *q++;
            fallback[k] = 0;
            value = substitute(trimmed(fallback, fallback + k), level + 1);
        } else if (value) value = substitute(value, level + 1);
        if (!value) return 0;   /* invalid at computed-value time */
        for (const char *c = value; *c && n < 2000; c++) out[n++] = *c;
        p = close;
    }
    out[n] = 0;
    return web_strdup(out, n);
}
/* Value tokens split on top-level spaces (and slashes when asked). */
static int split(const char *v, const char **parts, int max, int on_slash)
{
    int count = 0;
    const char *p = v;
    while (*p && count < max) {
        while (w_space(*p)) p++;
        if (!*p) break;
        if (on_slash && *p == '/') { parts[count++] = web_strdup("/", 1); p++; continue; }
        const char *start = p;
        int level = 0;
        while (*p && (level || (!w_space(*p) && !(on_slash && *p == '/') && !(on_slash == 2 && *p == ',')))) {
            if (*p == '(') level++;
            else if (*p == ')') level--;
            else if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q) p++; if (!*p) break; }
            p++;
        }
        parts[count++] = web_strdup(start, (int)(p - start));
        if (on_slash == 2 && *p == ',') { parts[count++] = web_strdup(",", 1); p++; }
    }
    return count;
}
static int is_color(const char *s) { int ok; w_parse_color(s, 0, &ok); return ok; }
static int is_border_style(const char *s)
{
    static const char *const names[] = {"none", "hidden", "solid", "dashed", "dotted", "double", "inset", "outset", "groove", "ridge"};
    for (int i = 0; i < 10; i++) if (w_eq_ci(s, names[i])) return 1;
    return 0;
}
static void four(int first, const char *v)   /* top right bottom left */
{
    const char *p[4];
    int n = split(v, p, 4, 0);
    if (!n) return;
    spec[first] = p[0];
    spec[first + 1] = n > 1 ? p[1] : p[0];
    spec[first + 2] = n > 2 ? p[2] : p[0];
    spec[first + 3] = n > 3 ? p[3] : n > 1 ? p[1] : p[0];
}
static void two(int a, int b, const char *v)
{
    const char *p[2];
    int n = split(v, p, 2, 0);
    if (!n) return;
    spec[a] = p[0]; spec[b] = n > 1 ? p[1] : p[0];
}
static void border_side(int side, const char *v)   /* side 0..3, or -1 for all */
{
    const char *p[6];
    int n = split(v, p, 6, 0);
    const char *width = "medium", *style = "none", *color = "currentcolor";
    for (int i = 0; i < n; i++) {
        if (is_border_style(p[i])) style = p[i];
        else if (is_color(p[i])) color = p[i];
        else width = p[i];
    }
    if (w_eq_ci(v, "none") || w_eq_ci(v, "0")) { style = "none"; width = "0"; }
    for (int s = 0; s < 4; s++) {
        if (side >= 0 && s != side) continue;
        spec[PR_BORDER_TOP_WIDTH + s] = width; spec[PR_BORDER_TOP_STYLE + s] = style; spec[PR_BORDER_TOP_COLOR + s] = color;
    }
}
static void apply(int prop, const char *value, const char *custom_name)
{
    if (prop == PR_CUSTOM) return;
    const char *v = substitute(value, 0);
    if (!v) return;
    if (prop < PR_LONGHANDS) { spec[prop] = v; return; }
    const char *p[8];
    int n;
    switch (prop) {
    case PR_MARGIN: four(PR_MARGIN_TOP, v); break;
    case PR_PADDING: four(PR_PADDING_TOP, v); break;
    case PR_INSET: four(PR_TOP, v); break;
    case PR_BORDER_WIDTH: four(PR_BORDER_TOP_WIDTH, v); break;
    case PR_BORDER_STYLE: four(PR_BORDER_TOP_STYLE, v); break;
    case PR_BORDER_COLOR: four(PR_BORDER_TOP_COLOR, v); break;
    case PR_BORDER: border_side(-1, v); break;
    case PR_BORDER_TOP: case PR_BORDER_RIGHT: case PR_BORDER_BOTTOM: case PR_BORDER_LEFT: border_side(prop - PR_BORDER_TOP, v); break;
    case PR_BORDER_BLOCK: border_side(0, v); border_side(2, v); break;
    case PR_BORDER_INLINE: border_side(1, v); border_side(3, v); break;
    case PR_MARGIN_BLOCK: two(PR_MARGIN_TOP, PR_MARGIN_BOTTOM, v); break;
    case PR_MARGIN_INLINE: two(PR_MARGIN_LEFT, PR_MARGIN_RIGHT, v); break;
    case PR_PADDING_BLOCK: two(PR_PADDING_TOP, PR_PADDING_BOTTOM, v); break;
    case PR_PADDING_INLINE: two(PR_PADDING_LEFT, PR_PADDING_RIGHT, v); break;
    case PR_INSET_INLINE: two(PR_LEFT, PR_RIGHT, v); break;
    case PR_INSET_BLOCK: two(PR_TOP, PR_BOTTOM, v); break;
    case PR_GAP: n = split(v, p, 2, 0); if (n) { spec[PR_ROW_GAP] = p[0]; spec[PR_COLUMN_GAP] = n > 1 ? p[1] : p[0]; } break;
    case PR_PLACE_ITEMS: n = split(v, p, 2, 0); if (n) spec[PR_ALIGN_ITEMS] = p[0]; break;
    case PR_PLACE_CONTENT: n = split(v, p, 2, 0); if (n) { spec[PR_ALIGN_CONTENT] = p[0]; spec[PR_JUSTIFY_CONTENT] = n > 1 ? p[1] : p[0]; } break;
    case PR_TEXT_WRAP: if (w_eq_ci(v, "nowrap")) spec[PR_WHITE_SPACE] = "nowrap"; break;
    case PR_OVERFLOW_WRAP: if (w_eq_ci(v, "anywhere") || w_eq_ci(v, "break-word")) spec[PR_WORD_BREAK] = "break-all"; break;
    case PR_BACKGROUND: {
        n = split(v, p, 8, 2);
        spec[PR_BACKGROUND_COLOR] = "transparent";
        spec[PR_BACKGROUND_IMAGE] = "none";
        for (int i = 0; i < n; i++) {
            if (w_eq(p[i], ",")) { spec[PR_BACKGROUND_COLOR] = "transparent"; continue; }   /* the color belongs to the last layer */
            if (w_starts_ci(p[i], "linear-gradient(") || w_starts_ci(p[i], "url(") || w_starts_ci(p[i], "radial-gradient(") ||
                w_starts_ci(p[i], "repeating-linear-gradient(")) { if (spec[PR_BACKGROUND_IMAGE][0] == 'n') spec[PR_BACKGROUND_IMAGE] = p[i]; }
            else if (is_color(p[i])) spec[PR_BACKGROUND_COLOR] = p[i];
        }
        break;
    }
    case PR_FONT: {
        n = split(v, p, 8, 1);
        int i = 0;
        if (n == 1 && !((p[0][0] >= '0' && p[0][0] <= '9'))) break;   /* system fonts: caption, menu... */
        spec[PR_FONT_STYLE] = "normal"; spec[PR_FONT_WEIGHT] = "normal"; spec[PR_LINE_HEIGHT] = "normal";
        for (; i < n; i++) {
            if (w_eq_ci(p[i], "italic") || w_eq_ci(p[i], "oblique")) spec[PR_FONT_STYLE] = p[i];
            else if (w_eq_ci(p[i], "bold") || w_eq_ci(p[i], "bolder") || w_eq_ci(p[i], "lighter") || (p[i][0] >= '1' && p[i][0] <= '9' && p[i][1] == '0' && p[i][2] == '0' && !p[i][3])) spec[PR_FONT_WEIGHT] = p[i];
            else if (w_eq_ci(p[i], "normal") || w_eq_ci(p[i], "small-caps")) continue;
            else break;
        }
        if (i < n) spec[PR_FONT_SIZE] = p[i++];
        if (i < n && w_eq(p[i], "/")) { i++; if (i < n) spec[PR_LINE_HEIGHT] = p[i++]; }
        if (i < n) {   /* the rest is the family list */
            const char *start = value;
            const char *f = w_find_ci(v, p[i]);
            spec[PR_FONT_FAMILY] = f ? f : start;
        }
        break;
    }
    case PR_FLEX: {
        n = split(v, p, 3, 0);
        if (n == 1 && w_eq_ci(p[0], "none")) { spec[PR_FLEX_GROW] = "0"; spec[PR_FLEX_SHRINK] = "0"; spec[PR_FLEX_BASIS] = "auto"; break; }
        if (n == 1 && w_eq_ci(p[0], "auto")) { spec[PR_FLEX_GROW] = "1"; spec[PR_FLEX_SHRINK] = "1"; spec[PR_FLEX_BASIS] = "auto"; break; }
        if (n == 1 && w_eq_ci(p[0], "initial")) { spec[PR_FLEX_GROW] = "0"; spec[PR_FLEX_SHRINK] = "1"; spec[PR_FLEX_BASIS] = "auto"; break; }
        spec[PR_FLEX_GROW] = "1"; spec[PR_FLEX_SHRINK] = "1"; spec[PR_FLEX_BASIS] = "0%";
        int numbers = 0;
        for (int i = 0; i < n; i++) {
            const char *t = p[i];
            int plain = 1;
            for (const char *c = t; *c; c++) if (!((*c >= '0' && *c <= '9') || *c == '.')) plain = 0;
            if (plain && numbers < 2) { spec[numbers ? PR_FLEX_SHRINK : PR_FLEX_GROW] = t; numbers++; }
            else spec[PR_FLEX_BASIS] = t;
        }
        break;
    }
    case PR_FLEX_FLOW:
        n = split(v, p, 2, 0);
        for (int i = 0; i < n; i++) { if (w_find_ci(p[i], "wrap")) spec[PR_FLEX_WRAP] = p[i]; else spec[PR_FLEX_DIRECTION] = p[i]; }
        break;
    case PR_LIST_STYLE:
        n = split(v, p, 3, 0);
        for (int i = 0; i < n; i++) {
            if (w_eq_ci(p[i], "inside") || w_eq_ci(p[i], "outside")) spec[PR_LIST_STYLE_POSITION] = p[i];
            else if (w_starts_ci(p[i], "url(")) spec[PR_LIST_STYLE_IMAGE] = p[i];
            else spec[PR_LIST_STYLE_TYPE] = p[i];
        }
        break;
    case PR_TEXT_DECORATION: {
        n = split(v, p, 4, 0);
        char lines[64] = "";
        for (int i = 0; i < n; i++) {
            if (w_eq_ci(p[i], "underline") || w_eq_ci(p[i], "line-through") || w_eq_ci(p[i], "overline") || w_eq_ci(p[i], "none")) { if (lines[0]) w_append(lines, " ", 64); w_append(lines, p[i], 64); }
            else if (is_color(p[i])) spec[PR_TEXT_DECORATION_COLOR] = p[i];
        }
        spec[PR_TEXT_DECORATION_LINE] = lines[0] ? web_strdup(lines, -1) : "none";
        break;
    }
    case PR_GRID_COLUMN: {
        const char *slash = v;
        while (*slash && *slash != '/') slash++;
        spec[PR_GRID_COLUMN_START] = web_strdup(v, (int)(slash - v));
        spec[PR_GRID_COLUMN_END] = *slash ? slash + 1 : "auto";
        break;
    }
    }
    (void)custom_name;
}

/* Presentational attributes (bgcolor, width, align, <font>...), applied
 * before the author's rules. */
static void hint(int prop, const char *value) { if (value && value[0]) apply(prop, value, 0); }
static const char *pixels_of(const char *v)
{
    if (!v) return 0;
    int digits = 0;
    while (v[digits] >= '0' && v[digits] <= '9') digits++;
    if (!digits) return 0;
    if (v[digits] == '%') return web_strdup(v, digits + 1);
    char *s = web_alloc((unsigned)digits + 3);
    for (int i = 0; i < digits; i++) s[i] = v[i];
    s[digits] = 'p'; s[digits + 1] = 'x';
    return s;
}
static const WNode *ancestor(const WNode *n, int tag)
{
    for (n = n->parent; n; n = n->parent) if (n->tag == tag) return n;
    return 0;
}
static void hints(const WNode *n)
{
    const char *v;
    if ((v = w_attr(n, "bgcolor"))) hint(PR_BACKGROUND_COLOR, v);
    if ((v = w_attr(n, "background")) == 0) {}
    switch (n->tag) {
    case T_BODY:
        if ((v = w_attr(n, "text"))) hint(PR_COLOR, v);
        if ((v = w_attr(n, "leftmargin")) || (v = w_attr(n, "marginwidth"))) { hint(PR_MARGIN_LEFT, pixels_of(v)); hint(PR_MARGIN_RIGHT, pixels_of(v)); }
        if ((v = w_attr(n, "topmargin")) || (v = w_attr(n, "marginheight"))) { hint(PR_MARGIN_TOP, pixels_of(v)); hint(PR_MARGIN_BOTTOM, pixels_of(v)); }
        break;
    case T_A: {
        const WNode *body = ancestor(n, T_BODY);
        if (body && (v = w_attr(body, "link")) && w_attr(n, "href")) hint(PR_COLOR, v);
        break;
    }
    case T_TABLE:
        if ((v = w_attr(n, "width"))) hint(PR_WIDTH, pixels_of(v));
        if ((v = w_attr(n, "height"))) hint(PR_HEIGHT, pixels_of(v));
        if ((v = w_attr(n, "cellspacing"))) hint(PR_BORDER_SPACING, pixels_of(v));
        if ((v = w_attr(n, "border")) && v[0] != '0') { hint(PR_BORDER, "1px outset #808080"); if (pixels_of(v)) hint(PR_BORDER_WIDTH, pixels_of(v)); }
        if ((v = w_attr(n, "align"))) { if (w_eq_ci(v, "center")) { hint(PR_MARGIN_LEFT, "auto"); hint(PR_MARGIN_RIGHT, "auto"); } else if (w_eq_ci(v, "left") || w_eq_ci(v, "right")) hint(PR_FLOAT, v); }
        break;
    case T_TD: case T_TH: case T_TR: case T_THEAD: case T_TBODY: case T_TFOOT: {
        if ((v = w_attr(n, "align"))) hint(PR_TEXT_ALIGN, v);
        if ((v = w_attr(n, "valign"))) hint(PR_VERTICAL_ALIGN, v);
        if (n->tag == T_TD || n->tag == T_TH) {
            if ((v = w_attr(n, "width"))) hint(PR_WIDTH, pixels_of(v));
            if ((v = w_attr(n, "height"))) hint(PR_HEIGHT, pixels_of(v));
            if (w_attr(n, "nowrap")) hint(PR_WHITE_SPACE, "nowrap");
            const WNode *table = ancestor(n, T_TABLE);
            if (table && (v = w_attr(table, "cellpadding"))) hint(PR_PADDING, pixels_of(v));
            if (table && (v = w_attr(table, "border")) && v[0] != '0') hint(PR_BORDER, "1px inset #808080");
        }
        if (n->tag == T_TR && (v = w_attr(n, "height"))) hint(PR_HEIGHT, pixels_of(v));
        break;
    }
    case T_DIV: case T_P: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_CAPTION: case T_LEGEND:
        if ((v = w_attr(n, "align"))) hint(PR_TEXT_ALIGN, w_eq_ci(v, "middle") ? "center" : v);
        break;
    case T_IMG: case T_IFRAME: case T_CANVAS: case T_VIDEO: case T_OBJECT: case T_EMBED: case T_INPUT:
        if (n->tag != T_INPUT || (w_attr(n, "type") && w_eq_ci(w_attr(n, "type"), "image"))) {
            if ((v = w_attr(n, "width"))) hint(PR_WIDTH, pixels_of(v));
            if ((v = w_attr(n, "height"))) hint(PR_HEIGHT, pixels_of(v));
        }
        if ((v = w_attr(n, "align")) && (w_eq_ci(v, "left") || w_eq_ci(v, "right"))) hint(PR_FLOAT, v);
        if ((v = w_attr(n, "border"))) { hint(PR_BORDER_WIDTH, pixels_of(v)); hint(PR_BORDER_STYLE, "solid"); }
        if ((v = w_attr(n, "hspace"))) { hint(PR_MARGIN_LEFT, pixels_of(v)); hint(PR_MARGIN_RIGHT, pixels_of(v)); }
        if ((v = w_attr(n, "vspace"))) { hint(PR_MARGIN_TOP, pixels_of(v)); hint(PR_MARGIN_BOTTOM, pixels_of(v)); }
        break;
    case T_FONT:
        if ((v = w_attr(n, "color"))) hint(PR_COLOR, v);
        if ((v = w_attr(n, "face"))) hint(PR_FONT_FAMILY, v);
        if ((v = w_attr(n, "size"))) {
            static const char *const sizes[8] = {"10px", "10px", "13px", "16px", "18px", "24px", "32px", "48px"};
            int k = 0, rel = 0;
            const char *p = v;
            if (*p == '+') { rel = 1; p++; } else if (*p == '-') { rel = -1; p++; }
            while (*p >= '0' && *p <= '9') k = k * 10 + *p++ - '0';
            if (rel) k = 3 + rel * k;
            if (k < 1) k = 1;
            if (k > 7) k = 7;
            hint(PR_FONT_SIZE, sizes[k]);
        }
        break;
    case T_HR:
        if ((v = w_attr(n, "width"))) hint(PR_WIDTH, pixels_of(v));
        if ((v = w_attr(n, "size"))) { hint(PR_HEIGHT, pixels_of(v)); }
        if ((v = w_attr(n, "color"))) { hint(PR_BORDER_COLOR, v); hint(PR_BACKGROUND_COLOR, v); }
        if (w_attr(n, "noshade")) { hint(PR_BORDER_STYLE, "solid"); hint(PR_BACKGROUND_COLOR, "gray"); }
        if ((v = w_attr(n, "align"))) { if (w_eq_ci(v, "left")) hint(PR_MARGIN_LEFT, "0"); else if (w_eq_ci(v, "right")) hint(PR_MARGIN_RIGHT, "0"); }
        break;
    case T_UL: case T_OL: case T_LI:
        if ((v = w_attr(n, "type"))) {
            const char *t = v[0] == '1' ? "decimal" : w_eq(v, "a") ? "lower-alpha" : w_eq(v, "A") ? "upper-alpha" :
                            w_eq(v, "i") ? "lower-roman" : w_eq(v, "I") ? "upper-roman" : v;
            hint(PR_LIST_STYLE_TYPE, t);
        }
        break;
    case T_PRE: if (w_attr(n, "wrap")) hint(PR_WHITE_SPACE, "pre-wrap"); break;
    }
}

/* ---------------------------------------------------------------- computed values */
static int keyword(const char *v, const char *const *names, int count, int fallback)
{
    if (!v) return fallback;
    for (int i = 0; i < count; i++) if (w_eq_ci(v, names[i])) return i;
    return fallback;
}
static int root_font = 16;
static int length_px(const char *v, int font, int fallback)
{
    int ok;
    if (!v) return fallback;
    if (w_eq_ci(v, "thin")) return 1;
    if (w_eq_ci(v, "medium")) return 3;
    if (w_eq_ci(v, "thick")) return 5;
    WLen l = w_parse_len(v, font, root_font, &ok);
    if (!ok || l.kind != WL_LEN) return fallback;
    return l.px;
}
static WLen length_of(const char *v, int font, WLen fallback)
{
    int ok;
    if (!v) return fallback;
    WLen l = w_parse_len(v, font, root_font, &ok);
    return ok ? l : fallback;
}
static int number_100(const char *v, int fallback)   /* "1.5" -> 150 */
{
    if (!v) return fallback;
    int sign = 1, whole = 0, frac = 0, scale = 10, any = 0;
    if (*v == '-') { sign = -1; v++; }
    while (*v >= '0' && *v <= '9') { whole = whole * 10 + *v++ - '0'; any = 1; }
    if (*v == '.') { v++; while (*v >= '0' && *v <= '9') { frac += (*v++ - '0') * scale; scale /= 10; any = 1; } }
    return any ? sign * (whole * 100 + frac) : fallback;
}
static int family_of(const char *v)
{
    while (v && *v) {
        while (w_space(*v) || *v == ',' || *v == '"' || *v == '\'') v++;
        const char *name = v;
        while (*v && *v != ',') v++;
        int n = (int)(v - name);
        char f[64]; int k = 0;
        for (int i = 0; i < n && k < 63; i++) if (name[i] != '"' && name[i] != '\'') f[k++] = (char)w_lower(name[i]);
        while (k && w_space(f[k - 1])) k--;
        f[k] = 0;
        if (w_find_ci(f, "mono") || w_find_ci(f, "courier") || w_find_ci(f, "consol") || w_find_ci(f, "menlo") || w_find_ci(f, "code") || w_find_ci(f, "lucida console")) return 2;
        if (w_eq(f, "serif") || w_find_ci(f, "times") || w_find_ci(f, "georgia") || w_find_ci(f, "garamond") || w_find_ci(f, "cambria") ||
            w_find_ci(f, "palatino") || w_find_ci(f, "book") || w_find_ci(f, "pt serif") || w_find_ci(f, "noto serif") || w_find_ci(f, "liberation serif")) return 1;
        if (w_eq(f, "sans-serif") || w_find_ci(f, "arial") || w_find_ci(f, "helvetica") || w_find_ci(f, "verdana") || w_find_ci(f, "tahoma") ||
            w_find_ci(f, "segoe") || w_find_ci(f, "roboto") || w_find_ci(f, "sans") || w_find_ci(f, "system-ui") || w_find_ci(f, "apple") ||
            w_find_ci(f, "inter") || w_find_ci(f, "ubuntu") || w_find_ci(f, "trebuchet") || w_find_ci(f, "lucida")) return 0;
    }
    return 0;
}
/* content: "text" attr(x) open-quote... into UTF-8. */
static const char *content_of(const char *v, const WNode *n)
{
    if (!v || w_eq_ci(v, "none") || w_eq_ci(v, "normal")) return 0;
    char out[512]; int k = 0;
    const char *p = v;
    while (*p && k < 500) {
        while (w_space(*p)) p++;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q && k < 500) {
                if (*p == '\\') {
                    p++;
                    int code = 0, digits = 0;
                    while (digits < 6 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) { code = code * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10); p++; digits++; }
                    if (digits) { if (*p == ' ') p++; if (code >= 0xe000 && code < 0xf900) continue; k += w_utf8_put(out + k, (unsigned)code); }   /* icon fonts: nothing */
                    else if (*p == 'A' || *p == 'a') { out[k++] = '\n'; p++; }
                    else if (*p) out[k++] = *p++;
                    continue;
                }
                out[k++] = *p++;
            }
            if (*p) p++;
        } else if (w_starts_ci(p, "attr(")) {
            p += 5;
            char name[64]; int m = 0;
            while (*p && *p != ')' && m < 63) { if (!w_space(*p)) name[m++] = (char)w_lower(*p); p++; }
            name[m] = 0;
            if (*p) p++;
            const char *a = w_attr(n, name);
            for (; a && *a && k < 500; a++) out[k++] = *a;
        } else if (w_starts_ci(p, "open-quote")) { k += w_utf8_put(out + k, 0x201c); p += 10; }
        else if (w_starts_ci(p, "close-quote")) { k += w_utf8_put(out + k, 0x201d); p += 11; }
        else if (w_starts_ci(p, "counter")) { p = skip_balanced(p + 7, p + w_len(p)); }
        else { while (*p && !w_space(*p)) p++; }
    }
    return web_strdup(out, k);
}
static void gradient_of(WStyle *st, const char *v)
{
    if (!v || !w_starts_ci(v, "linear-gradient(") ) return;
    const char *parts[16];
    const char *inner = v + 16;
    int n = split(inner, parts, 16, 2);
    unsigned int colors[8]; int count = 0, angle = 180;
    int first = 1;
    for (int i = 0; i < n; i++) {
        const char *t = parts[i];
        int l = w_len(t);
        if (l && t[l - 1] == ')' && i == n - 1) { char *c = web_strdup(t, l - 1); t = c; }
        if (w_eq(t, ",")) { first = 0; continue; }
        if (first && w_eq_ci(t, "to")) continue;
        if (first && (w_eq_ci(t, "right") || w_eq_ci(t, "left") || w_eq_ci(t, "bottom") || w_eq_ci(t, "top"))) {
            angle = w_eq_ci(t, "right") ? 90 : w_eq_ci(t, "left") ? 270 : w_eq_ci(t, "top") ? 0 : 180;
            continue;
        }
        if (first && w_find_ci(t, "deg")) { angle = number_100(t, 18000) / 100; continue; }
        int ok;
        unsigned int c = w_parse_color(t, st->color, &ok);
        if (ok && count < 8) colors[count++] = c;
    }
    if (count >= 1) {
        st->gradient[0] = colors[0]; st->gradient[1] = colors[count - 1];
        st->gradient_angle = angle; st->has_gradient = 1;
    }
}
static void tracks_of(WStyle *st, const char *v)
{
    st->column_count = 0; st->column_auto_fill = 0;
    if (!v || w_eq_ci(v, "none")) return;
    if (!st->columns) st->columns = web_alloc(sizeof(WTrack) * W_TRACKS);
    const char *parts[W_TRACKS * 2];
    int n = split(v, parts, W_TRACKS * 2, 0);
    for (int i = 0; i < n && st->column_count < W_TRACKS; i++) {
        const char *t = parts[i];
        if (t[0] == '[') continue;   /* line names */
        if (w_starts_ci(t, "repeat(")) {
            const char *p = t + 7;
            int count = 0;
            int fill = w_starts_ci(p, "auto-fill") || w_starts_ci(p, "auto-fit");
            if (!fill) while (*p >= '0' && *p <= '9') count = count * 10 + *p++ - '0';
            while (*p && *p != ',') p++;
            if (*p) p++;
            int l = w_len(p);
            char *inner = web_strdup(p, l > 0 ? l - 1 : 0);
            WStyle tmp; tmp.column_count = 0; tmp.columns = 0;
            tmp.font = st->font;
            tracks_of(&tmp, inner);
            if (fill && tmp.column_count) {
                st->column_auto_fill = 1;
                st->column_auto_min = tmp.columns[0].min.kind == WL_LEN ? tmp.columns[0].min : tmp.columns[0].max;
                st->columns[0] = tmp.columns[0];
                st->column_count = 1;
                return;
            }
            for (int r = 0; r < count && st->column_count < W_TRACKS; r++)
                for (int k = 0; k < tmp.column_count && st->column_count < W_TRACKS; k++) st->columns[st->column_count++] = tmp.columns[k];
            continue;
        }
        WTrack track;
        WLen autolen = {WL_AUTO, 0, 0, 0};
        track.min = autolen; track.max = autolen; track.fr = 0;
        const char *body = t;
        if (w_starts_ci(t, "minmax(")) {
            const char *p = t + 7;
            const char *comma = p;
            while (*comma && *comma != ',') comma++;
            track.min = length_of(web_strdup(p, (int)(comma - p)), st->font.size, autolen);
            body = *comma ? comma + 1 : comma;
            while (w_space(*body)) body++;
        }
        int fr = 0;
        const char *e = body;
        while ((*e >= '0' && *e <= '9') || *e == '.') e++;
        if (e > body && (e[0] == 'f' || e[0] == 'F') && (e[1] == 'r' || e[1] == 'R')) fr = number_100(body, 100);
        if (fr) track.fr = fr;
        else track.max = length_of(body, st->font.size, autolen);
        if (body == t) track.min = track.max;
        st->columns[st->column_count++] = track;
    }
}
static int grid_line(const char *v, int *span)
{
    *span = 0;
    if (!v) return 0;
    while (w_space(*v)) v++;
    if (w_starts_ci(v, "span")) { v += 4; while (w_space(*v)) v++; *span = number_100(v, 100) / 100; return 0; }
    int value = number_100(v, 0) / 100;
    return value;
}

static void compute(WNode *n, WStyle *st, const WStyle *parent, const char **s)
{
    static const char *const displays[] = {"none", "inline", "block", "inline-block", "list-item", "flex", "inline-flex", "grid", "inline-grid",
                                          "table", "inline-table", "table-row", "table-cell", "table-row-group", "table-caption", "table-column",
                                          "flow-root", "contents"};
    /* inheritance first */
    WStyle initial;
    const WStyle *p = parent;
    if (!p) {
        for (unsigned int i = 0; i < sizeof(initial); i++) ((char *)&initial)[i] = 0;
        initial.color = 0xff000000u;
        initial.font.size = 16 * (w_doc ? w_doc->zoom : 100) / 100;
        initial.font_weight = 400;
        initial.border_spacing = 0;
        initial.opacity = 255;
        p = &initial;
    }
    st->color = p->color; st->font = p->font; st->font_weight = p->font_weight; st->line_height = p->line_height;
    st->letter_spacing = p->letter_spacing; st->word_spacing = p->word_spacing; st->text_align = p->text_align;
    st->text_transform = p->text_transform; st->white_space = p->white_space; st->list_style = p->list_style;
    st->list_inside = p->list_inside; st->visibility = p->visibility; st->text_indent = p->text_indent;
    st->border_spacing = p->border_spacing; st->border_collapse = p->border_collapse; st->decoration = p->decoration;
    st->decoration_color = p->decoration_color; st->word_break = p->word_break; st->list_image = p->list_image;
    st->caption_bottom = p->caption_bottom; st->center_blocks = p->center_blocks;
    st->custom = customs;
    st->opacity = 255;
#define INHERIT(prop) (s[prop] && (w_eq_ci(s[prop], "inherit") || (inherited(prop) && (w_eq_ci(s[prop], "unset") || w_eq_ci(s[prop], "revert") || w_eq_ci(s[prop], "revert-layer")))))
#define INITIAL(prop) (s[prop] && (w_eq_ci(s[prop], "initial") || (!inherited(prop) && (w_eq_ci(s[prop], "unset") || w_eq_ci(s[prop], "revert")))))
    for (int i = 0; i < PR_LONGHANDS; i++) {
        if (INHERIT(i) && parent) {   /* explicit inheritance of a reset property */
            switch (i) {
            case PR_DISPLAY: s[i] = 0; st->display = parent->display; continue;
            case PR_BACKGROUND_COLOR: s[i] = 0; st->background = parent->background; continue;
            case PR_HEIGHT: s[i] = 0; st->height = parent->height; continue;
            case PR_WIDTH: s[i] = 0; st->width = parent->width; continue;
            }
            s[i] = 0;   /* inherited properties already are */
        } else if (INITIAL(i) || (s[i] && INHERIT(i))) s[i] = inherited(i) ? "initial" : 0;
    }
    int parent_size = p->font.size ? p->font.size : 16;
    /* font size first: em units depend on it */
    if (s[PR_FONT_SIZE]) {
        const char *v = s[PR_FONT_SIZE];
        static const char *const words[] = {"xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large"};
        static const int sizes[] = {9, 10, 13, 16, 18, 24, 32, 48};
        int k = keyword(v, words, 8, -1);
        int z = w_doc ? w_doc->zoom : 100;
        if (k >= 0) st->font.size = sizes[k] * z / 100;
        else if (w_eq_ci(v, "smaller")) st->font.size = parent_size * 5 / 6;
        else if (w_eq_ci(v, "larger")) st->font.size = parent_size * 6 / 5;
        else if (w_eq_ci(v, "initial")) st->font.size = 16 * z / 100;
        else {
            int ok;
            WLen l = w_parse_len(v, parent_size, root_font, &ok);
            if (ok && l.kind == WL_LEN) st->font.size = l.px + (int)((long long)l.pct * parent_size / 10000);
            else if (ok) st->font.size = w_resolve(&l, parent_size, parent_size);
        }
        if (st->font.size < 1) st->font.size = 1;
        if (st->font.size > 400) st->font.size = 400;
    }
    int fs = st->font.size;
    if (n->tag == T_HTML) root_font = fs;
    if (s[PR_FONT_FAMILY]) st->font.family = (unsigned char)(w_eq_ci(s[PR_FONT_FAMILY], "initial") ? 0 : family_of(s[PR_FONT_FAMILY]));
    if (s[PR_FONT_WEIGHT]) {
        const char *v = s[PR_FONT_WEIGHT];
        if (w_eq_ci(v, "bold")) st->font_weight = 700;
        else if (w_eq_ci(v, "normal") || w_eq_ci(v, "initial")) st->font_weight = 400;
        else if (w_eq_ci(v, "bolder")) st->font_weight = p->font_weight < 600 ? 700 : 900;
        else if (w_eq_ci(v, "lighter")) st->font_weight = p->font_weight > 500 ? 400 : 100;
        else { int w = number_100(v, 40000) / 100; st->font_weight = (short)(w < 1 ? 400 : w); }
    }
    st->font.bold = st->font_weight >= 600;
    if (s[PR_FONT_STYLE]) st->font.italic = w_eq_ci(s[PR_FONT_STYLE], "italic") || w_starts_ci(s[PR_FONT_STYLE], "oblique");
    if (s[PR_LINE_HEIGHT]) {
        const char *v = s[PR_LINE_HEIGHT];
        if (w_eq_ci(v, "normal") || w_eq_ci(v, "initial")) st->line_height = 0;
        else {
            int plain = 1;
            for (const char *c = v; *c; c++) if (!((*c >= '0' && *c <= '9') || *c == '.')) plain = 0;
            if (plain) st->line_height = -number_100(v, 120);   /* a factor, inherited as such */
            else { int ok; WLen l = w_parse_len(v, fs, root_font, &ok); if (ok) st->line_height = w_resolve(&l, fs, fs * 6 / 5); if (st->line_height <= 0) st->line_height = 1; }
        }
    }
    if (s[PR_COLOR]) { int ok; unsigned int c = w_parse_color(s[PR_COLOR], p->color, &ok); if (ok) st->color = c; else if (w_eq_ci(s[PR_COLOR], "initial")) st->color = 0xff000000u; }
    st->decoration_color = 0;
    if (s[PR_LETTER_SPACING]) st->letter_spacing = w_eq_ci(s[PR_LETTER_SPACING], "normal") || w_eq_ci(s[PR_LETTER_SPACING], "initial") ? 0 : length_px(s[PR_LETTER_SPACING], fs, 0);
    if (s[PR_WORD_SPACING]) st->word_spacing = w_eq_ci(s[PR_WORD_SPACING], "normal") ? 0 : length_px(s[PR_WORD_SPACING], fs, 0);
    if (s[PR_TEXT_ALIGN]) {
        const char *v = s[PR_TEXT_ALIGN];
        st->center_blocks = w_eq_ci(v, "-webkit-center") || w_eq_ci(v, "-moz-center");
        st->text_align = w_eq_ci(v, "right") || w_eq_ci(v, "end") || w_eq_ci(v, "-webkit-right") ? TA_RIGHT :
                         w_eq_ci(v, "center") || w_eq_ci(v, "-webkit-center") || w_eq_ci(v, "-moz-center") ? TA_CENTER :
                         w_eq_ci(v, "justify") ? TA_JUSTIFY : TA_LEFT;
    }
    if (s[PR_TEXT_TRANSFORM]) { static const char *const t[] = {"none", "uppercase", "lowercase", "capitalize"}; st->text_transform = (unsigned char)keyword(s[PR_TEXT_TRANSFORM], t, 4, 0); }
    if (s[PR_WHITE_SPACE]) {
        const char *v = s[PR_WHITE_SPACE];
        st->white_space = w_eq_ci(v, "pre") || w_eq_ci(v, "preserve") || w_eq_ci(v, "break-spaces") ? (w_eq_ci(v, "pre") ? WS_PRE : WS_PRE_WRAP) :
                          w_eq_ci(v, "nowrap") ? WS_NOWRAP : w_eq_ci(v, "pre-wrap") ? WS_PRE_WRAP : w_eq_ci(v, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
    }
    if (s[PR_CAPTION_SIDE]) st->caption_bottom = w_eq_ci(s[PR_CAPTION_SIDE], "bottom");
    if (s[PR_WORD_BREAK]) st->word_break = w_eq_ci(s[PR_WORD_BREAK], "break-all") || w_eq_ci(s[PR_WORD_BREAK], "break-word");
    if (s[PR_LIST_STYLE_TYPE]) {
        static const char *const t[] = {"none", "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman"};
        const char *v = s[PR_LIST_STYLE_TYPE];
        int k = keyword(v, t, 9, -1);
        if (k < 0) k = w_eq_ci(v, "lower-latin") ? LS_LOWER_ALPHA : w_eq_ci(v, "upper-latin") ? LS_UPPER_ALPHA :
                       w_eq_ci(v, "decimal-leading-zero") ? LS_DECIMAL : v[0] == '"' ? LS_DISC : w_eq_ci(v, "initial") ? LS_DISC : LS_DECIMAL;
        st->list_style = (unsigned char)k;
    }
    if (s[PR_LIST_STYLE_POSITION]) st->list_inside = w_eq_ci(s[PR_LIST_STYLE_POSITION], "inside");
    if (s[PR_VISIBILITY]) st->visibility = w_eq_ci(s[PR_VISIBILITY], "hidden") || w_eq_ci(s[PR_VISIBILITY], "collapse");
    if (s[PR_TEXT_INDENT]) { WLen zero = {WL_LEN, 0, 0, 0}; st->text_indent = length_of(s[PR_TEXT_INDENT], fs, zero); }
    if (s[PR_BORDER_SPACING]) st->border_spacing = length_px(s[PR_BORDER_SPACING], fs, 0);
    if (s[PR_BORDER_COLLAPSE]) st->border_collapse = w_eq_ci(s[PR_BORDER_COLLAPSE], "collapse");
    if (s[PR_TEXT_DECORATION_LINE]) {
        const char *v = s[PR_TEXT_DECORATION_LINE];
        if (w_find_ci(v, "none") || w_eq_ci(v, "initial")) st->decoration = 0;
        if (w_find_ci(v, "underline")) st->decoration |= TD_UNDERLINE;
        if (w_find_ci(v, "line-through")) st->decoration |= TD_LINE_THROUGH;
        if (w_find_ci(v, "overline")) st->decoration |= TD_OVERLINE;
    }
    if (s[PR_TEXT_DECORATION_COLOR]) { int ok; unsigned int c = w_parse_color(s[PR_TEXT_DECORATION_COLOR], st->color, &ok); if (ok) st->decoration_color = c; }

    /* reset properties */
    int k = keyword(s[PR_DISPLAY], displays, 18, -1);
    if (k < 0 && s[PR_DISPLAY]) {   /* two-value display and friends */
        const char *v = s[PR_DISPLAY];
        int inl = w_find_ci(v, "inline") != 0;
        if (w_find_ci(v, "flex")) k = inl ? D_INLINE_FLEX : D_FLEX;
        else if (w_find_ci(v, "grid")) k = inl ? D_INLINE_GRID : D_GRID;
        else if (w_find_ci(v, "table-header-group") || w_find_ci(v, "table-footer-group")) k = D_TABLE_ROW_GROUP;
        else if (w_find_ci(v, "table-column")) k = D_TABLE_COLUMN;
        else if (w_find_ci(v, "list-item")) k = D_LIST_ITEM;
        else if (w_find_ci(v, "flow-root")) k = inl ? D_INLINE_BLOCK : D_FLOW_ROOT;
        else if (w_find_ci(v, "-webkit-box")) k = inl ? D_INLINE_FLEX : D_BLOCK;
        else if (w_find_ci(v, "block")) k = D_BLOCK;
        else if (w_find_ci(v, "ruby")) k = D_INLINE;
        else if (w_find_ci(v, "inline")) k = D_INLINE;
    }
    st->display = (unsigned char)(k >= 0 ? k : D_INLINE);
    if (s[PR_POSITION]) { static const char *const t[] = {"static", "relative", "absolute", "fixed", "sticky", "-webkit-sticky"}; int v = keyword(s[PR_POSITION], t, 6, 0); st->position = (unsigned char)(v == 5 ? P_STICKY : v); }
    if (s[PR_FLOAT]) st->floating = w_eq_ci(s[PR_FLOAT], "left") || w_eq_ci(s[PR_FLOAT], "inline-start") ? 1 : w_eq_ci(s[PR_FLOAT], "right") || w_eq_ci(s[PR_FLOAT], "inline-end") ? 2 : 0;
    if (s[PR_CLEAR]) st->clear = w_eq_ci(s[PR_CLEAR], "left") ? 1 : w_eq_ci(s[PR_CLEAR], "right") ? 2 : w_eq_ci(s[PR_CLEAR], "both") ? 3 : 0;
    if (s[PR_BOX_SIZING]) st->box_sizing = w_eq_ci(s[PR_BOX_SIZING], "border-box");
    if (s[PR_OVERFLOW]) st->overflow = !w_eq_ci(s[PR_OVERFLOW], "visible") && !w_eq_ci(s[PR_OVERFLOW], "initial");
    if (s[PR_VERTICAL_ALIGN]) {
        static const char *const t[] = {"baseline", "sub", "super", "middle", "top", "bottom", "text-top", "text-bottom"};
        st->vertical_align = (unsigned char)keyword(s[PR_VERTICAL_ALIGN], t, 8, 0);
    }
    if (s[PR_FLEX_DIRECTION]) { static const char *const t[] = {"row", "row-reverse", "column", "column-reverse"}; st->flex_direction = (unsigned char)keyword(s[PR_FLEX_DIRECTION], t, 4, 0); }
    if (s[PR_FLEX_WRAP]) st->flex_wrap = w_eq_ci(s[PR_FLEX_WRAP], "wrap") || w_eq_ci(s[PR_FLEX_WRAP], "wrap-reverse");
    static const char *const align[] = {"flex-start", "flex-end", "center", "space-between", "space-around", "space-evenly", "stretch", "baseline",
                                       "start", "end", "normal", "left", "right", "self-start", "self-end", "first baseline"};
    static const unsigned char align_map[] = {JC_START, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY, JC_STRETCH, JC_BASELINE,
                                              JC_START, JC_END, JC_STRETCH, JC_START, JC_END, JC_START, JC_END, JC_BASELINE};
    int a;
    st->justify = JC_START; st->align_items = JC_STRETCH; st->align_self = 255; st->align_content = JC_STRETCH;
    if (s[PR_JUSTIFY_CONTENT]) {
        const char *v = s[PR_JUSTIFY_CONTENT];
        if (w_starts_ci(v, "safe ") || w_starts_ci(v, "unsafe ")) { while (*v && *v != ' ') v++; while (w_space(*v)) v++; }
        a = keyword(v, align, 16, 0);
        st->justify = a == 10 ? JC_START : align_map[a];
    }
    if (s[PR_ALIGN_ITEMS]) { a = keyword(s[PR_ALIGN_ITEMS], align, 16, 10); st->align_items = align_map[a]; }
    if (s[PR_ALIGN_SELF] && !w_eq_ci(s[PR_ALIGN_SELF], "auto")) { a = keyword(s[PR_ALIGN_SELF], align, 16, 10); st->align_self = align_map[a]; }
    if (s[PR_ALIGN_CONTENT]) { a = keyword(s[PR_ALIGN_CONTENT], align, 16, 10); st->align_content = align_map[a]; }
    st->background = 0;
    if (s[PR_BACKGROUND_COLOR]) { int ok; unsigned int c = w_parse_color(s[PR_BACKGROUND_COLOR], st->color, &ok); if (ok) st->background = c; }
    st->has_gradient = 0;
    if (s[PR_BACKGROUND_IMAGE]) gradient_of(st, s[PR_BACKGROUND_IMAGE]);
    static const char *const bstyles[] = {"none", "solid", "dashed", "dotted", "double", "inset", "outset", "groove", "ridge", "hidden"};
    for (int side = 0; side < 4; side++) {
        int bs = keyword(s[PR_BORDER_TOP_STYLE + side], bstyles, 10, 0);
        st->border_style[side] = (unsigned char)(bs == 9 ? 0 : bs);
        st->border_width[side] = bs && bs != 9 ? length_px(s[PR_BORDER_TOP_WIDTH + side] ? s[PR_BORDER_TOP_WIDTH + side] : "medium", fs, 3) : 0;
        if (st->border_width[side] < 0) st->border_width[side] = 0;
        int ok = 0;
        unsigned int c = s[PR_BORDER_TOP_COLOR + side] ? w_parse_color(s[PR_BORDER_TOP_COLOR + side], st->color, &ok) : 0;
        st->border_color[side] = ok ? c : st->color;
    }
    st->radius = 0;
    if (s[PR_BORDER_RADIUS]) {
        int ok;
        WLen l = w_parse_len(s[PR_BORDER_RADIUS], fs, root_font, &ok);
        if (ok && l.kind == WL_LEN) st->radius = l.pct >= 4000 ? 10000 : l.px + (l.pct ? l.pct / 400 : 0);
    }
    WLen autolen = {WL_AUTO, 0, 0, 0}, zero = {WL_LEN, 0, 0, 0}, none = {WL_NONE, 0, 0, 0};
    st->width = length_of(s[PR_WIDTH], fs, autolen);
    st->height = length_of(s[PR_HEIGHT], fs, autolen);
    st->min_width = length_of(s[PR_MIN_WIDTH], fs, autolen);
    st->max_width = length_of(s[PR_MAX_WIDTH], fs, none);
    st->min_height = length_of(s[PR_MIN_HEIGHT], fs, autolen);
    st->max_height = length_of(s[PR_MAX_HEIGHT], fs, none);
    if (st->min_width.kind == WL_NONE) st->min_width = autolen;
    if (st->max_width.kind == WL_AUTO) st->max_width = none;
    for (int i = 0; i < 4; i++) {
        st->margin[i] = length_of(s[PR_MARGIN_TOP + i], fs, zero);
        st->padding[i] = length_of(s[PR_PADDING_TOP + i], fs, zero);
        if (st->padding[i].kind != WL_LEN && st->padding[i].kind < WL_MIN) st->padding[i] = zero;
        st->offset[i] = length_of(s[PR_TOP + i], fs, autolen);
    }
    st->flex_grow = number_100(s[PR_FLEX_GROW], 0);
    st->flex_shrink = number_100(s[PR_FLEX_SHRINK], 100);
    st->flex_basis = length_of(s[PR_FLEX_BASIS], fs, autolen);
    if (s[PR_FLEX_BASIS] && w_eq_ci(s[PR_FLEX_BASIS], "content")) st->flex_basis = autolen;
    st->order = number_100(s[PR_ORDER], 0) / 100;
    st->gap_row = s[PR_ROW_GAP] && !w_eq_ci(s[PR_ROW_GAP], "normal") ? length_px(s[PR_ROW_GAP], fs, 0) : 0;
    st->gap_column = s[PR_COLUMN_GAP] && !w_eq_ci(s[PR_COLUMN_GAP], "normal") ? length_px(s[PR_COLUMN_GAP], fs, 0) : 0;
    st->z_index = number_100(s[PR_Z_INDEX], 0) / 100;
    if (s[PR_OPACITY]) {
        int v = number_100(s[PR_OPACITY], 100);
        const char *q = s[PR_OPACITY];
        while (*q && *q != '%') q++;
        if (*q == '%') v /= 100;
        st->opacity = (unsigned char)(v <= 0 ? 0 : v >= 100 ? 255 : v * 255 / 100);
    }
    st->content = n->pseudo == 1 || n->pseudo == 2 ? content_of(s[PR_CONTENT], n->parent) : 0;
    st->columns = 0; st->column_count = 0;
    if (s[PR_GRID_TEMPLATE_COLUMNS]) tracks_of(st, s[PR_GRID_TEMPLATE_COLUMNS]);
    st->masked = s[PR_MASK] && !w_eq_ci(s[PR_MASK], "none") && !w_eq_ci(s[PR_MASK], "initial");
    {
        int span1, span2;
        int start = grid_line(s[PR_GRID_COLUMN_START], &span1), end = grid_line(s[PR_GRID_COLUMN_END], &span2);
        st->column_start = start > 0 ? start : 0;
        st->column_span = span1 ? span1 : span2 ? span2 : start > 0 && end > start ? end - start : end < 0 ? -1 : 1;
    }
    if (s[PR_LIST_STYLE_IMAGE]) st->list_image = w_eq_ci(s[PR_LIST_STYLE_IMAGE], "none") ? 0 : s[PR_LIST_STYLE_IMAGE];

    /* blockification: floats, absolute boxes, flex and grid items */
    int blockify = st->floating || st->position == P_ABSOLUTE || st->position == P_FIXED ||
                   (parent && (parent->display == D_FLEX || parent->display == D_INLINE_FLEX || parent->display == D_GRID || parent->display == D_INLINE_GRID));
    if (blockify) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK || st->display == D_TABLE_CELL || st->display == D_TABLE_ROW) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    if (st->position == P_ABSOLUTE || st->position == P_FIXED) st->floating = 0;
    if (n->tag == T_HTML || n->tag == T_BODY) st->overflow = 0;
    if (n->tag == T_HTML && st->display == D_INLINE) st->display = D_BLOCK;
}

/* Runs the cascade for one element (or pseudo-element) into st. */
static WDecl inline_decls[128];
static void cascade(WNode *n, int pseudo, WStyle *st, const WStyle *parent, WCustom *inherited_customs)
{
    for (int i = 0; i < PR_COUNT; i++) spec[i] = 0;
    customs = inherited_customs;
    const WNode *element_node = pseudo ? n->parent : n;
    collect(element_node, pseudo);
    int inline_count = 0;
    const char *style_attr = pseudo ? 0 : w_attr(n, "style");
    if (style_attr) inline_count = parse_declarations(style_attr, style_attr + w_len(style_attr), inline_decls, 128);
    /* custom properties first: other values refer to them */
    for (int important = 0; important < 2; important++) {
        for (int i = 0; i < match_count; i++) {
            const WRule *r = matches[i].rule;
            for (int d = 0; d < r->decl_count; d++)
                if (r->decls[d].prop == PR_CUSTOM && r->decls[d].important == important) {
                    WCustom *c = web_alloc(sizeof(WCustom));
                    c->name = r->decls[d].name; c->value = r->decls[d].value; c->next = customs; customs = c;
                }
        }
        for (int d = 0; d < inline_count; d++)
            if (inline_decls[d].prop == PR_CUSTOM && inline_decls[d].important == important) {
                WCustom *c = web_alloc(sizeof(WCustom));
                c->name = inline_decls[d].name; c->value = inline_decls[d].value; c->next = customs; customs = c;
            }
    }
    /* user agent rules, presentational hints, author rules, inline style */
    int i = 0;
    for (; i < match_count && matches[i].rule->origin == 0; i++)
        for (int d = 0; d < matches[i].rule->decl_count; d++) if (!matches[i].rule->decls[d].important) apply(matches[i].rule->decls[d].prop, matches[i].rule->decls[d].value, matches[i].rule->decls[d].name);
    if (!pseudo) hints(n);
    for (; i < match_count; i++)
        for (int d = 0; d < matches[i].rule->decl_count; d++) if (!matches[i].rule->decls[d].important) apply(matches[i].rule->decls[d].prop, matches[i].rule->decls[d].value, matches[i].rule->decls[d].name);
    for (int d = 0; d < inline_count; d++) if (!inline_decls[d].important) apply(inline_decls[d].prop, inline_decls[d].value, 0);
    for (int k = 0; k < match_count; k++)
        if (matches[k].rule->origin)
            for (int d = 0; d < matches[k].rule->decl_count; d++) if (matches[k].rule->decls[d].important) apply(matches[k].rule->decls[d].prop, matches[k].rule->decls[d].value, 0);
    for (int d = 0; d < inline_count; d++) if (inline_decls[d].important) apply(inline_decls[d].prop, inline_decls[d].value, 0);
    for (int k = 0; k < match_count; k++)
        if (!matches[k].rule->origin)
            for (int d = 0; d < matches[k].rule->decl_count; d++) if (matches[k].rule->decls[d].important) apply(matches[k].rule->decls[d].prop, matches[k].rule->decls[d].value, 0);
    compute(n, st, parent, spec);
}

static WNode *pseudo_node(WNode *n, int kind)
{
    WStyle *st = web_alloc(sizeof(WStyle));
    WNode *p = web_alloc(sizeof(WNode));
    p->type = WN_ELEMENT; p->pseudo = (unsigned char)kind; p->name = kind == 1 ? "::before" : "::after";
    p->parent = n;
    cascade(p, kind, st, n->style, n->style->custom);
    if (!st->content || st->display == D_NONE) return 0;
    p->style = st;
    if (st->content[0]) {
        WNode *t = web_alloc(sizeof(WNode));
        t->type = WN_TEXT; t->text = (char *)st->content; t->length = w_len(st->content); t->parent = p; t->style = st;
        p->first = p->last = t;
    }
    return p;
}
static void link_first(WNode *parent, WNode *c) { c->parent = parent; c->next = parent->first; if (parent->first) parent->first->prev = c; else parent->last = c; parent->first = c; }
static void link_last(WNode *parent, WNode *c) { c->parent = parent; c->prev = parent->last; if (parent->last) parent->last->next = c; else parent->first = c; parent->last = c; }
static void unlink_pseudo(WNode *n)
{
    while (n->first && n->first->pseudo == 1) { WNode *c = n->first; n->first = c->next; if (n->first) n->first->prev = 0; else n->last = 0; }
    while (n->last && n->last->pseudo == 2) { WNode *c = n->last; n->last = c->prev; if (n->last) n->last->next = 0; else n->first = 0; }
}

void web_css_reset(WDoc *d)
{
    doc = d;
    d->rules = 0; d->rule_count = d->rule_capacity = 0; d->rule_order = 0;
    d->index = web_alloc_big(sizeof(Index));
    origin = 0;
    media_depth = 0;
    web_add_stylesheet(d, ua_css, w_len(ua_css), d->url, 0);
    if (d->quirks) {   /* HTML: tables do not inherit text and font settings in quirks mode */
        static const char quirks_css[] = "table{font-size:initial;font-weight:initial;font-style:initial;line-height:normal;white-space:initial;text-align:initial}";
        web_add_stylesheet(d, quirks_css, w_len(quirks_css), d->url, 0);
    }
    origin = 2;
}

/* Styles every node, in document order without recursion. */
void web_style(WDoc *d)
{
    doc = d; w_doc = d;
    root_font = 16 * d->zoom / 100;
    WNode *n = d->root;
    while (n) {
        if (n->type == WN_ELEMENT && !n->pseudo) {
            unlink_pseudo(n);
            WStyle *st = web_alloc(sizeof(WStyle));
            const WStyle *parent = n->parent ? n->parent->style : 0;
            cascade(n, 0, st, parent, parent ? parent->custom : 0);
            st->custom = customs;
            n->style = st;
            if (st->display != D_NONE && n->tag != T_INPUT && n->tag != T_IMG && n->tag != T_SELECT && n->tag != T_TEXTAREA) {
                WNode *b = pseudo_node(n, 1), *a = pseudo_node(n, 2);
                if (b) link_first(n, b);
                if (a) link_last(n, a);
            }
            if (st->display == D_NONE) {   /* nothing below is drawn */
                while (n && !n->next && n != d->root) n = n->parent;
                if (!n || n == d->root) break;
                n = n->next;
                continue;
            }
        } else if (n->type == WN_TEXT) n->style = n->parent ? n->parent->style : 0;
        else if (n->pseudo && n->first) n->first->style = n->style;
        if (n->first && !(n->pseudo)) { n = n->first; continue; }
        while (n && !n->next && n != d->root) n = n->parent;
        if (!n || n == d->root) break;
        n = n->next;
    }
}
