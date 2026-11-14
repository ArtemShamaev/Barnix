/* Gouse Web engine: HTML5 parsing, CSS3 cascade, layout and painting.
 * Independent of Happy Gouse, so tests/web_test.c runs it on the host:
 * the platform provides web_fetch, web_inflate and web_progress. All
 * page memory comes from one arena that is reset for every page; fonts
 * and their glyph cache live in memory given to web_font_init. Integer
 * arithmetic only (pixels; font advances in 1/16 px). */
#ifndef WEB_H
#define WEB_H
#define WEB_RESOURCE_MAX (16 << 20)

typedef struct { unsigned int *pixels; int width, height; } WCanvas;   /* same layout as BgxCanvas */

/* ---------------------------------------------------------------- platform */
typedef struct {
    char *data;            /* allocated with web_alloc */
    int length;
    int status;            /* HTTP status, 200 for files */
    char url[512];         /* final URL after redirects */
    char error[128];
    char type[96];         /* Content-Type, "" when unknown */
} WebResource;
/* Fetches http(s):, file: or a local path into the page arena. 0 or -1. */
int web_fetch(const char *url, WebResource *out, int max_bytes, const char *post_body, const char *post_type);
long web_inflate(const void *in, unsigned long in_length, void *out, unsigned long capacity, int zlib);
void web_progress(const char *text);
/* Called often during long script work: keeps the desktop alive; nonzero stops the page's scripts. */
int web_idle(void);
unsigned int web_time(void);   /* milliseconds: the clock web_js_tick is given */

/* ---------------------------------------------------------------- memory */
void web_arena(void *memory, unsigned int bytes);
void *web_alloc(unsigned int bytes);            /* zeroed; small requests never fail */
void *web_alloc_big(unsigned int bytes);        /* 0 when the arena is full */
unsigned int web_mark(void);
void *web_take(unsigned int bytes);            /* not zeroed */
void web_release(unsigned int mark);
unsigned int web_used(void);
extern int web_out_of_memory;
char *web_strdup(const char *s, int length);

/* ---------------------------------------------------------------- strings */
int w_len(const char *s);
void w_copy(char *out, const char *s, int capacity);
void w_append(char *out, const char *s, int capacity);
int w_eq(const char *a, const char *b);
int w_eqn_ci(const char *a, const char *b, int n);     /* first n bytes, ASCII case-insensitive */
int w_eq_ci(const char *a, const char *b);
int w_starts_ci(const char *s, const char *prefix);
const char *w_find_ci(const char *s, const char *needle);
int w_space(int c);
int w_lower(int c);
int w_itoa(char *out, int value);
int w_utf8_decode(const unsigned char **p, const unsigned char *end);   /* next code point, U+FFFD for bad bytes */
int w_utf8_put(char *out, unsigned int code);
void w_resolve_url(const char *base, const char *href, char *out, int capacity);
void w_url_encode(const char *s, char *out, int capacity);

/* ---------------------------------------------------------------- fonts */
enum { WF_SANS, WF_SANS_BOLD, WF_SERIF, WF_MONO, WF_FACES };
/* data: the zlib file of scripts/web_font.py; memory holds the inflated
 * font and the glyph cache. 0 or -1. */
int web_font_init(const void *data, unsigned int length, void *memory, unsigned int bytes);
int web_font_ready(void);
typedef struct { unsigned char family, bold, italic; int size; } WFont;   /* family: 0 sans, 1 serif, 2 mono */
int wf_advance(const WFont *f, unsigned int code);   /* 1/16 px */
int wf_ascent(const WFont *f);
int wf_descent(const WFont *f);
int wf_text_width(const WFont *f, const char *text, int length, int letter_spacing);   /* 1/16 px */
void wf_draw(WCanvas *c, int x, int y_baseline, const WFont *f, const char *text, int length,
             unsigned int argb, int letter_spacing, int clip_x0, int clip_y0, int clip_x1, int clip_y1);

/* ---------------------------------------------------------------- DOM */
enum { WN_ELEMENT = 1, WN_TEXT = 3 };
typedef struct WAttr { const char *name, *value; struct WAttr *next; } WAttr;
typedef struct WStyle WStyle;
typedef struct WControl WControl;
typedef struct WImage { int width, height; unsigned int *pixels; } WImage;   /* 0xAARRGGBB */
typedef struct WNode {
    unsigned char type, pseudo;          /* pseudo: 1 ::before, 2 ::after, 3 anonymous box */
    unsigned short tag;
    const char *name;
    struct WNode *parent, *first, *last, *next, *prev;
    struct WNode *anon_stop;              /* anonymous boxes: their children run first..anon_stop */
    WAttr *attrs;
    const char *id, *class_list;          /* class attribute */
    char *text; int length;               /* text nodes */
    WStyle *style;
    WControl *control;
    WImage *image;
    int x, y, w, h;                       /* border box after layout (document coordinates) */
    int item_first, item_last;            /* its display items */
    int bg_item, border_item, content_item;
    int static_x, static_y;               /* absolutely positioned: where it would have been */
    int imark, imin, imax;                /* intrinsic widths cache */
    int js_id;                            /* its handle in the page's JavaScript, 0 = none */
} WNode;
const char *w_attr(const WNode *n, const char *name);
int w_has_class(const WNode *n, const char *name);

/* Tags the engine treats specially */
enum {
    T_UNKNOWN, T_HTML, T_HEAD, T_BODY, T_TITLE, T_META, T_LINK, T_STYLE, T_SCRIPT, T_NOSCRIPT, T_BASE,
    T_DIV, T_SPAN, T_P, T_A, T_B, T_STRONG, T_I, T_EM, T_U, T_S, T_SMALL, T_BIG, T_SUB, T_SUP, T_CODE,
    T_PRE, T_BLOCKQUOTE, T_BR, T_HR, T_IMG, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_UL, T_OL, T_LI, T_DL,
    T_DT, T_DD, T_TABLE, T_CAPTION, T_THEAD, T_TBODY, T_TFOOT, T_TR, T_TD, T_TH, T_COL, T_COLGROUP,
    T_FORM, T_INPUT, T_BUTTON, T_SELECT, T_OPTION, T_OPTGROUP, T_TEXTAREA, T_LABEL, T_FIELDSET, T_LEGEND,
    T_HEADER, T_FOOTER, T_NAV, T_MAIN, T_SECTION, T_ARTICLE, T_ASIDE, T_FIGURE, T_FIGCAPTION, T_ADDRESS,
    T_CENTER, T_FONT, T_IFRAME, T_SVG, T_MATH, T_CANVAS, T_VIDEO, T_AUDIO, T_OBJECT, T_EMBED, T_TEMPLATE,
    T_WBR, T_AREA, T_PARAM, T_SOURCE, T_TRACK, T_DETAILS, T_SUMMARY, T_MENU, T_DIR, T_PICTURE, T_Q,
    T_NOBR, T_MARK, T_COUNT
};

/* ---------------------------------------------------------------- CSS values */
enum { WL_AUTO, WL_LEN, WL_NONE, WL_MIN, WL_MAX, WL_CLAMP, WL_CONTENT };   /* WL_CONTENT: min/max/fit-content */
typedef struct WLen {
    unsigned char kind;
    int px;            /* WL_LEN: px + pct * containing / 10000 */
    int pct;           /* in 1/100 of a percent */
    struct WLen *args; /* WL_MIN/MAX: two, WL_CLAMP: three */
} WLen;
int w_resolve(const WLen *l, int containing, int fallback);

enum { D_NONE, D_INLINE, D_BLOCK, D_INLINE_BLOCK, D_LIST_ITEM, D_FLEX, D_INLINE_FLEX, D_GRID, D_INLINE_GRID,
       D_TABLE, D_INLINE_TABLE, D_TABLE_ROW, D_TABLE_CELL, D_TABLE_ROW_GROUP, D_TABLE_CAPTION, D_TABLE_COLUMN,
       D_FLOW_ROOT, D_CONTENTS };
enum { P_STATIC, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum { WS_NORMAL, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE };
enum { TA_LEFT, TA_RIGHT, TA_CENTER, TA_JUSTIFY };
enum { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_INSET, BS_OUTSET, BS_GROOVE, BS_RIDGE };
enum { LS_NONE, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LOWER_ALPHA, LS_UPPER_ALPHA, LS_LOWER_ROMAN, LS_UPPER_ROMAN };
enum { VA_BASELINE, VA_SUB, VA_SUPER, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_TEXT_TOP, VA_TEXT_BOTTOM };
enum { JC_START, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY, JC_STRETCH, JC_BASELINE };
#define TD_UNDERLINE 1
#define TD_LINE_THROUGH 2
#define TD_OVERLINE 4
#define W_TRACKS 16
typedef struct { WLen min, max; int fr; } WTrack;   /* fr in 1/100 */

struct WStyle {
    unsigned char display, position, floating, clear, visibility, white_space, text_align, text_transform;
    unsigned char list_style, list_inside, box_sizing, overflow, vertical_align, flex_direction, flex_wrap;
    unsigned char justify, align_items, align_self, align_content, decoration, border_collapse, opacity;
    unsigned char border_style[4], word_break, masked, caption_bottom, center_blocks;
    WFont font;
    short font_weight;
    int line_height;        /* px; 0 = normal */
    int letter_spacing;     /* px */
    int word_spacing;
    unsigned int color, background, border_color[4], decoration_color;
    unsigned int gradient[2]; int gradient_angle, has_gradient;
    int border_width[4], radius;
    WLen width, height, min_width, max_width, min_height, max_height;
    WLen margin[4], padding[4], offset[4];   /* top right bottom left */
    WLen flex_basis, text_indent;
    int flex_grow, flex_shrink, order;       /* grow/shrink in 1/100 */
    int gap_row, gap_column, border_spacing;
    int z_index;
    WTrack *columns; int column_count, column_auto_fill; WLen column_auto_min;   /* grid-template-columns */
    int column_span, column_start;
    const char *content;    /* ::before/::after text, 0 = none */
    struct WCustom *custom; /* custom properties in scope */
    const char *list_image;
};
typedef struct WCustom { const char *name, *value; struct WCustom *next; } WCustom;

/* ---------------------------------------------------------------- forms */
enum { WC_TEXT, WC_PASSWORD, WC_CHECKBOX, WC_RADIO, WC_SUBMIT, WC_RESET, WC_BUTTON, WC_HIDDEN, WC_SELECT, WC_TEXTAREA, WC_IMAGE, WC_FILE };
struct WControl {
    int type;
    WNode *node, *form;
    char *value; int capacity, cursor;
    int checked, selected, option_count;
};

/* ---------------------------------------------------------------- display list */
enum { WI_NONE, WI_RECT, WI_BORDER, WI_TEXT, WI_LINE, WI_IMAGE, WI_CLIP, WI_UNCLIP, WI_CONTROL, WI_MARKER };
typedef struct {
    unsigned char kind, fixed, italic, style;   /* style: border style / marker type */
    short size;
    int x, y, w, h;
    unsigned int color, color2;
    int radius;
    WNode *node, *link;                  /* node: owner; link: <a href> around it */
    const char *text; int length;
    unsigned char family, bold;
    short letter_spacing;
    int widths[4]; unsigned int colors[4];   /* borders */
    WImage *image;
} WItem;

/* ---------------------------------------------------------------- the document */
typedef struct WRule WRule;
typedef struct WDoc {
    void *js;
    WNode *root, *head, *body;
    char url[512], base[512], title[256];
    WRule **rules; int rule_count, rule_capacity, rule_order;
    void *index;                          /* selector buckets */
    WItem *items; int item_count, item_capacity;
    int width, height, viewport_h, zoom;  /* zoom in percent */
    unsigned int canvas_color;
    int focused;                          /* index of the focused control, -1 */
    WControl **controls; int control_count;
    int layout_pass;
    unsigned int mark;                    /* the arena after loading: styles and layout come after it */
    int prepared, quirks;                  /* quirks: no <!DOCTYPE html> */
    int find_item;
    int charset;                          /* WCS_*: the page's, for its scripts */
} WDoc;

/* Parsing: html is UTF-8 (web_to_utf8 converts legacy encodings first). */
char *web_to_utf8(const char *data, int length, int *out_length);
enum { WCS_UTF8, WCS_1251, WCS_KOI8R, WCS_LATIN1 };
int web_charset_of(const char *text, int length);    /* from "charset=..." in it */
char *web_convert(const char *data, int length, int charset, int *out_length);
int w_valid_utf8(const char *data, int length);
WDoc *web_parse(const char *html, int length, const char *url);
/* Stylesheets: the user agent sheet, <style> and <link rel=stylesheet>
 * (fetched with web_fetch), images (PNG). */
void web_load_resources(WDoc *doc, int images);
void web_add_stylesheet(WDoc *doc, const char *css, int length, const char *base, int depth);
/* Cascade and layout for a viewport; may be repeated (resizing). */
void web_layout(WDoc *doc, int width, int viewport_h);
void web_unlayout(WDoc *doc);
void web_js_load(WDoc *doc);
void web_js_destroy(WDoc *doc);
/* Runs due timers and frames: 0 nothing changed, 1 repaint, 2 the layout must be redone. */
int web_js_tick(WDoc *doc, unsigned int now, int width, int height);
int web_js_event(WDoc *doc, WNode *target, const char *type, int x, int y, int key);
int web_js_changes(WDoc *doc);   /* of the last tick or event, as web_js_tick returns */
const char *web_js_error(WDoc *doc);
void web_paint(WDoc *doc, WCanvas *c, int x, int y, int w, int h, int scroll);
/* What is at x, y of the document (y already scrolled): */
WItem *web_hit(WDoc *doc, int x, int y, int scroll);
int web_anchor_y(WDoc *doc, const char *fragment);   /* -1 when there is none */
int web_find(WDoc *doc, const char *text, int from_item);   /* item index or -1 */
/* Forms */
int web_submit(WDoc *doc, WControl *submitter, char *url, int capacity, char *body, int body_capacity);   /* 0 GET, 1 POST, -1 */
WImage *web_png(const unsigned char *data, int length);
const char *web_text_of(const WNode *n, char *out, int capacity);   /* concatenated text */
/* querySelectorAll: elements below scope matching a selector list, in document
 * order; -1 for a selector the engine does not understand. The arena is left as it was. */
int web_query(WDoc *doc, WNode *scope, const char *selector, WNode **out, int max);

/* internal between the engine's files */
void web_css_reset(WDoc *doc);
void web_style(WDoc *doc);
void web_layout_tree(WDoc *doc);
void web_prepare_controls(WDoc *doc);
int w_tag_of(const char *name);
const char *w_tag_name(int tag);
WLen w_parse_len(const char *s, int font_size, int root_size, int *ok);
unsigned int w_parse_color(const char *s, unsigned int current, int *ok);
WItem *w_item(WDoc *doc, int kind);
extern WDoc *w_doc;     /* the document being styled or laid out */
#endif
