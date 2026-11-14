/* Gouse Paint: tools, palette, undo, and 24-bit BMP files. */
#include "gouse.h"

#define IMG_MAX_W 512
#define IMG_MAX_H 400
#define TOOLBOX 60
#define PALETTE_H 44
enum { T_PENCIL, T_BRUSH, T_ERASER, T_LINE, T_RECT, T_FRECT, T_ELLIPSE, T_FELLIPSE, T_FILL, T_PICK, T_SPRAY, T_TEXT, TOOLS };
static const char *const tool_names[TOOLS] = {"Pencil", "Brush", "Eraser", "Line", "Rectangle", "Filled rectangle",
                                              "Ellipse", "Filled ellipse", "Fill", "Pick color", "Spray", "Text"};
static const unsigned int palette[28] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080, 0x808040, 0x004040, 0x0080ff, 0x004080, 0x8000ff, 0x804000,
    0xffffff, 0xc0c0c0, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0xff00ff, 0xffff80, 0x00ff80, 0x80ffff, 0x8080ff, 0xff0080, 0xff8040};
enum { ID_TOOL = 1, ID_SIZE = 40, ID_COLOR = 60, M_NEW = 100, M_OPEN, M_SAVE, M_SAVEAS, M_UNDO, M_CLEAR, M_INVERT, M_FLIPH, M_FLIPV, M_EXIT,
       P_NEW = 150, P_OPEN, P_SAVE, P_TEXT };
typedef struct {
    unsigned int image[IMG_MAX_W * IMG_MAX_H], undo[IMG_MAX_W * IMG_MAX_H];
    unsigned int stack[IMG_MAX_W * 8];
    int iw, ih, tool, size, drawing, x0, y0, lx, ly, mx, my, ox, oy;
    unsigned int primary, secondary, color;
    char path[256], text[128];
    int modified, has_undo;
} State;
#define S ((State *)w->state)
#define AT(s, x, y) (s)->image[(y) * (s)->iw + (x)]

static void put(State *s, int x, int y, unsigned int c) { if ((unsigned)x < (unsigned)s->iw && (unsigned)y < (unsigned)s->ih) AT(s, x, y) = c; }
static BgxCanvas image_canvas(State *s) { BgxCanvas c = {s->image, s->iw, s->ih}; return c; }
static void stamp(State *s, int x, int y, unsigned int c)
{
    if (s->tool == T_ERASER) { BgxCanvas ic = image_canvas(s); bgx_fill(&ic, x - s->size * 2, y - s->size * 2, s->size * 4 + 1, s->size * 4 + 1, c); }
    else if (s->tool == T_BRUSH) { BgxCanvas ic = image_canvas(s); g_disc(&ic, x, y, s->size, c); }
    else if (s->size <= 1) put(s, x, y, c);
    else { BgxCanvas ic = image_canvas(s); bgx_fill(&ic, x - s->size / 2, y - s->size / 2, s->size, s->size, c); }
}
static void stroke(State *s, int x0, int y0, int x1, int y1, unsigned int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, steps = dx > dy ? dx : dy;
    if (!steps) steps = 1;
    for (int i = 0; i <= steps; i++) stamp(s, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c);
}
static void save_undo(State *s) { for (int i = 0; i < s->iw * s->ih; i++) s->undo[i] = s->image[i]; s->has_undo = 1; }
static void restore_undo(State *s) { for (int i = 0; i < s->iw * s->ih; i++) s->image[i] = s->undo[i]; }
static void shape(State *s, int x1, int y1)
{
    BgxCanvas ic = image_canvas(s);
    int x0 = s->x0, y0 = s->y0, t = s->size;
    if (g_shift() && s->tool != T_LINE) {   /* squares and circles */
        int d = (x1 - x0 > 0 ? x1 - x0 : x0 - x1) < (y1 - y0 > 0 ? y1 - y0 : y0 - y1) ? (x1 - x0 > 0 ? x1 - x0 : x0 - x1) : (y1 - y0 > 0 ? y1 - y0 : y0 - y1);
        x1 = x0 + (x1 >= x0 ? d : -d); y1 = y0 + (y1 >= y0 ? d : -d);
    }
    int l = x0 < x1 ? x0 : x1, r = x0 < x1 ? x1 : x0, top = y0 < y1 ? y0 : y1, b = y0 < y1 ? y1 : y0;
    switch (s->tool) {
    case T_LINE: g_thick_line(&ic, x0, y0, x1, y1, t, s->color); break;
    case T_RECT: for (int k = 0; k < t; k++) bgx_frame(&ic, l + k, top + k, r - l + 1 - 2 * k, b - top + 1 - 2 * k, 1, s->color); break;
    case T_FRECT: bgx_fill(&ic, l, top, r - l + 1, b - top + 1, s->color); break;
    case T_ELLIPSE: for (int k = 0; k < t; k++) g_ellipse(&ic, l + k, top + k, r - k, b - k, s->color, 0); break;
    case T_FELLIPSE: g_ellipse(&ic, l, top, r, b, s->color, 1); break;
    }
}
/* Scanline flood fill with an explicit stack of seeds. */
static void fill(State *s, int x, int y, unsigned int c)
{
    unsigned int target = AT(s, x, y);
    if (target == c) return;
    int top = 0, capacity = (int)(sizeof(s->stack) / sizeof(s->stack[0]));
    s->stack[top++] = (unsigned int)y << 16 | (unsigned int)x;
    while (top) {
        unsigned int seed = s->stack[--top];
        int sx = seed & 0xffff, sy = seed >> 16;
        if (AT(s, sx, sy) != target) continue;
        int a = sx, b = sx;
        while (a > 0 && AT(s, a - 1, sy) == target) a--;
        while (b < s->iw - 1 && AT(s, b + 1, sy) == target) b++;
        for (int i = a; i <= b; i++) AT(s, i, sy) = c;
        for (int d = -1; d <= 1; d += 2) {
            int ny = sy + d;
            if (ny < 0 || ny >= s->ih) continue;
            for (int i = a; i <= b; i++)
                if (AT(s, i, ny) == target && (i == a || AT(s, i - 1, ny) != target) && top < capacity)
                    s->stack[top++] = (unsigned int)ny << 16 | (unsigned int)i;
        }
    }
}
static void new_image(State *s, int width, int height)
{
    if (width < 8) width = 8;
    if (height < 8) height = 8;
    if (width > IMG_MAX_W) width = IMG_MAX_W;
    if (height > IMG_MAX_H) height = IMG_MAX_H;
    s->iw = width; s->ih = height;
    for (int i = 0; i < width * height; i++) s->image[i] = 0xffffff;
    s->has_undo = 0; s->modified = 0;
}
static unsigned int le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }
static void put32(unsigned char *p, unsigned int v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void update_title(GWindow *w)
{
    char title[300]; const char *name = S->path;
    for (const char *p = S->path; *p; p++) if (*p == '/') name = p + 1;
    g_format(title, sizeof(title), "Paint - %s%s", S->path[0] ? name : g_tr("(untitled)"), S->modified ? " *" : "");
    if (!g_equal(title, w->title)) g_set_title(w, title);
}
static void load_bmp(GWindow *w, const char *path)
{
    State *s = S;
    enum { DATA = IMG_MAX_W * IMG_MAX_H * 4 + 128 };
    static unsigned char *data;   /* taken once, from Gouse's memory: the program arena is small */
    if (!data) data = g_alloc(DATA);
    if (!data) { g_error(w, "Paint", "Not enough memory."); return; }
    int n = g_read_file(path, (char *)data, DATA);
    if (n < 54 || data[0] != 'B' || data[1] != 'M') { g_error(w, "Paint", "Not a BMP picture."); return; }
    unsigned int offset = le32(data + 10), width = le32(data + 18), bpp = data[28] | data[29] << 8, compression = le32(data + 30);
    int height = (int)le32(data + 22), top_down = height < 0;
    if (top_down) height = -height;
    if ((bpp != 24 && bpp != 32) || compression > 3 || !width || width > 4096 || height > 4096) { g_error(w, "Paint", "Only 24- and 32-bit uncompressed BMP files."); return; }
    unsigned int stride = (width * (bpp / 8) + 3) & ~3U;
    new_image(s, (int)width, height);
    for (int y = 0; y < s->ih; y++) {
        int row = top_down ? y : height - 1 - y;
        unsigned int at = offset + (unsigned int)row * stride;
        for (int x = 0; x < s->iw; x++) {
            unsigned int p = at + (unsigned int)x * (bpp / 8);
            if (p + 2 < (unsigned int)n) AT(s, x, y) = data[p + 2] << 16 | data[p + 1] << 8 | data[p];
        }
    }
    g_copy(s->path, path, sizeof(s->path));
    update_title(w);
}
static void save_bmp(GWindow *w, const char *path)
{
    State *s = S;
    static unsigned char *data;
    if (!data) data = g_alloc(IMG_MAX_W * IMG_MAX_H * 3 + 64);
    if (!data) { g_error(w, "Paint", "Not enough memory."); return; }
    unsigned int stride = ((unsigned int)s->iw * 3 + 3) & ~3U, size = 54 + stride * s->ih;
    for (unsigned int i = 0; i < size; i++) data[i] = 0;
    data[0] = 'B'; data[1] = 'M'; put32(data + 2, size); put32(data + 10, 54); put32(data + 14, 40);
    put32(data + 18, s->iw); put32(data + 22, s->ih); data[26] = 1; data[28] = 24; put32(data + 34, stride * s->ih);
    put32(data + 38, 2835); put32(data + 42, 2835);
    for (int y = 0; y < s->ih; y++)
        for (int x = 0; x < s->iw; x++) {
            unsigned char *p = data + 54 + (s->ih - 1 - y) * stride + x * 3;
            unsigned int c = AT(s, x, y);
            p[0] = c; p[1] = c >> 8; p[2] = c >> 16;
        }
    if (g_write_file(path, (const char *)data, size)) { g_error(w, "Paint", "Cannot save the picture there (640 KiB per file)."); return; }
    g_copy(s->path, path, sizeof(s->path));
    s->modified = 0;
    update_title(w);
}
static void view_origin(GWindow *w, int *x, int *y)
{
    State *s = S;
    int vw = w->w - TOOLBOX - 8, vh = w->h - PALETTE_H - 8;
    *x = TOOLBOX + 4 + (s->iw < vw ? (vw - s->iw) / 2 : -s->ox);
    *y = 4 + (s->ih < vh ? (vh - s->ih) / 2 : -s->oy);
}
static void tool_icon(BgxCanvas *c, int t, int x, int y)
{
    unsigned int ink = 0x202020;
    switch (t) {
    case T_PENCIL: g_thick_line(c, x + 4, y + 15, x + 14, y + 5, 3, 0xe0a020); g_line(c, x + 3, y + 16, x + 5, y + 14, ink); break;
    case T_BRUSH: g_thick_line(c, x + 9, y + 11, x + 15, y + 3, 2, 0x8a5a33); g_disc(c, x + 7, y + 13, 3, 0x3060d0); break;
    case T_ERASER: bgx_fill(c, x + 4, y + 7, 12, 8, 0xf0a0b0); bgx_frame(c, x + 4, y + 7, 12, 8, 1, ink); bgx_fill(c, x + 4, y + 7, 5, 8, 0x80a0f0); break;
    case T_LINE: g_thick_line(c, x + 3, y + 16, x + 16, y + 4, 2, ink); break;
    case T_RECT: bgx_frame(c, x + 3, y + 5, 14, 11, 2, ink); break;
    case T_FRECT: bgx_fill(c, x + 3, y + 5, 14, 11, 0x3060d0); bgx_frame(c, x + 3, y + 5, 14, 11, 1, ink); break;
    case T_ELLIPSE: g_ellipse(c, x + 3, y + 5, x + 16, y + 15, ink, 0); g_ellipse(c, x + 4, y + 6, x + 15, y + 14, ink, 0); break;
    case T_FELLIPSE: g_ellipse(c, x + 3, y + 5, x + 16, y + 15, 0x3060d0, 1); g_ellipse(c, x + 3, y + 5, x + 16, y + 15, ink, 0); break;
    case T_FILL: bgx_fill(c, x + 5, y + 6, 9, 9, 0xd0d0d0); bgx_frame(c, x + 5, y + 6, 9, 9, 1, ink); g_disc(c, x + 15, y + 14, 2, 0x3060d0); break;
    case T_PICK: g_thick_line(c, x + 5, y + 15, x + 13, y + 7, 2, ink); g_disc(c, x + 14, y + 6, 3, ink); break;
    case T_SPRAY: for (int k = 0; k < 9; k++) bgx_fill(c, x + 4 + (k * 7) % 12, y + 4 + (k * 5) % 12, 2, 2, ink); break;
    case T_TEXT: bgx_text(c, x + 6, y + 2, "A", ink, 1); break;
    }
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    /* tool box */
    for (int t = 0; t < TOOLS; t++) {
        int x = 6 + t % 2 * 26, y = 6 + t / 2 * 26;
        g_panel(c, x, y, 24, 24, t == s->tool ? 0xffffff : g_theme.face, t == s->tool || w->pressed_zone == ID_TOOL + t);
        tool_icon(c, t, x + 2, y + 2);
        g_zone(w, ID_TOOL + t, x, y, 24, 24);
    }
    static const int sizes[4] = {1, 2, 4, 8};
    int sy = 6 + (TOOLS + 1) / 2 * 26 + 6;
    g_panel(c, 6, sy, 50, 4 * 18 + 4, 0xffffff, 1);
    for (int k = 0; k < 4; k++) {
        int y = sy + 2 + k * 18;
        if (s->size == sizes[k]) bgx_fill(c, 8, y, 46, 18, g_theme.highlight);
        bgx_fill(c, 12, y + 9 - sizes[k] / 2, 38, sizes[k], s->size == sizes[k] ? 0xffffff : 0);
        g_zone(w, ID_SIZE + k, 6, y, 50, 18);
    }
    /* picture */
    int vx = TOOLBOX, vw = w->w - TOOLBOX - 4, vh = w->h - PALETTE_H - 4;
    g_panel(c, vx, 2, vw, vh, 0x808890, 1);
    int ix, iy; view_origin(w, &ix, &iy);
    for (int y = 0; y < s->ih; y++) {
        int dy = iy + y;
        if (dy < 4 || dy >= vh) continue;
        int x0 = ix < vx + 2 ? vx + 2 - ix : 0, x1 = ix + s->iw > vx + vw - 2 ? vx + vw - 2 - ix : s->iw;
        unsigned int *out = c->pixels + dy * c->width + ix;
        for (int x = x0; x < x1; x++) out[x] = AT(s, x, y);
    }
    /* palette */
    int py = w->h - PALETTE_H + 4;
    g_panel(c, 6, py, 34, 34, g_theme.face, 1);
    bgx_fill(c, 18, py + 14, 16, 16, s->secondary); bgx_frame(c, 18, py + 14, 16, 16, 1, 0);
    bgx_fill(c, 10, py + 5, 16, 16, s->primary); bgx_frame(c, 10, py + 5, 16, 16, 1, 0);
    for (int k = 0; k < 28; k++) {
        int x = 48 + k % 14 * 18, y = py + k / 14 * 18;
        bgx_fill(c, x, y, 16, 16, palette[k]); g_bevel(c, x, y, 16, 16, 1);
        g_zone(w, ID_COLOR + k, x, y, 16, 16);
    }
    char status[96];
    if (s->mx >= 0) g_format(status, sizeof(status), "%s   %d,%d   %dx%d", g_tr(tool_names[s->tool]), s->mx, s->my, s->iw, s->ih);
    else g_format(status, sizeof(status), "%s   %dx%d", g_tr(tool_names[s->tool]), s->iw, s->ih);
    int sx = 48 + 14 * 18 + 8;
    if (sx < w->w - 60) g_text_fit(c, sx, py + 10, w->w - sx - 8, status, g_theme.text);
    update_title(w);
}
static void begin(GWindow *w, int x, int y, unsigned int button)
{
    State *s = S;
    s->color = button == 2 ? s->secondary : s->primary;
    if ((unsigned)x >= (unsigned)s->iw || (unsigned)y >= (unsigned)s->ih) return;
    if (s->tool == T_PICK) { if (button == 2) s->secondary = AT(s, x, y); else s->primary = AT(s, x, y); return; }
    save_undo(s); s->modified = 1;
    if (s->tool == T_FILL) { fill(s, x, y, s->color); return; }
    if (s->tool == T_TEXT) {
        if (s->text[0]) { BgxCanvas ic = image_canvas(s); (bgx_text)(&ic, x, y - 8, s->text, s->color, s->size > 2 ? 2 : 1); }
        else g_prompt(w, P_TEXT, "Text", "Text to place (then click the picture):", "Honk!");
        return;
    }
    s->drawing = 1; s->x0 = s->lx = x; s->y0 = s->ly = y;
    if (s->tool == T_ERASER) s->color = button == 2 ? s->primary : s->secondary;
    if (s->tool <= T_ERASER) stamp(s, x, y, s->color);
}
static void tick(GWindow *w, int dt)
{
    State *s = S;
    if (!s->drawing || s->tool != T_SPRAY) return;
    int count = dt / 4 + 1, r = s->size * 4 + 4;
    for (int i = 0; i < count; i++) {
        int dx = (int)(g_random() % (2 * r + 1)) - r, dy = (int)(g_random() % (2 * r + 1)) - r;
        if (dx * dx + dy * dy <= r * r) put(s, s->lx + dx, s->ly + dy, s->color);
    }
    g_invalidate(w);
}
static void open(GWindow *w, const char *argument)
{
    State *s = S;
    s->primary = 0; s->secondary = 0xffffff; s->size = 1; s->mx = -1;
    new_image(s, 400, 280);
    if (argument) load_bmp(w, argument); else update_title(w);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int ix, iy; view_origin(w, &ix, &iy);
    int x = e->x - ix, y = e->y - iy;
    int in_view = e->x >= TOOLBOX && e->y < w->h - PALETTE_H;
    if (e->type == EV_COMMAND) {
        if (e->id >= ID_TOOL && e->id < ID_TOOL + TOOLS) { s->tool = e->id - ID_TOOL; if (s->tool == T_TEXT) s->text[0] = 0; }
        else if (e->id >= ID_SIZE && e->id < ID_SIZE + 4) { static const int sizes[4] = {1, 2, 4, 8}; s->size = sizes[e->id - ID_SIZE]; }
        else if (e->id >= ID_COLOR && e->id < ID_COLOR + 28) s->primary = palette[e->id - ID_COLOR];
    } else if (e->type == EV_MOUSE_DOWN) {
        if (!in_view) {   /* right click on the palette picks the second color */
            for (int k = 0; k < 28 && e->button == 2; k++) {
                int px = 48 + k % 14 * 18, py = w->h - PALETTE_H + 4 + k / 14 * 18;
                if (e->x >= px && e->y >= py && e->x < px + 16 && e->y < py + 16) s->secondary = palette[k];
            }
            return;
        }
        begin(w, x, y, e->button);
    } else if (e->type == EV_MOUSE_MOVE) {
        s->mx = (unsigned)x < (unsigned)s->iw && (unsigned)y < (unsigned)s->ih ? x : -1; s->my = y;
        if (!s->drawing) return;
        if (s->tool <= T_ERASER) stroke(s, s->lx, s->ly, x, y, s->color);
        else if (s->tool >= T_LINE && s->tool <= T_FELLIPSE) { restore_undo(s); shape(s, x, y); }
        s->lx = x; s->ly = y;
    } else if (e->type == EV_MOUSE_UP && s->drawing) {
        if (s->tool >= T_LINE && s->tool <= T_FELLIPSE) { restore_undo(s); shape(s, x, y); }
        s->drawing = 0;
    } else if (e->type == EV_WHEEL) {
        if (g_shift()) s->ox += e->id * 24; else s->oy += e->id * 24;
        int vw = w->w - TOOLBOX - 8, vh = w->h - PALETTE_H - 8;
        if (s->ox > s->iw - vw) s->ox = s->iw - vw;
        if (s->oy > s->ih - vh) s->oy = s->ih - vh;
        if (s->ox < 0) s->ox = 0;
        if (s->oy < 0) s->oy = 0;
    } else if (e->type == EV_KEY) {
        if (e->key == 26 && s->has_undo) { for (int i = 0; i < s->iw * s->ih; i++) { unsigned int t = s->image[i]; s->image[i] = s->undo[i]; s->undo[i] = t; } }
        else if (e->key == 19) { if (s->path[0]) save_bmp(w, s->path); else g_prompt(w, P_SAVE, "Save As", "BMP file (path):", ""); }
        else if (e->key >= '1' && e->key <= '9' && e->key - '1' < TOOLS) s->tool = e->key - '1';
    } else if (e->type == EV_MENU) {
        char path[256];
        switch (e->id) {
        case M_NEW: g_prompt(w, P_NEW, "New picture", "Size (width x height, up to 512x400):", "400x280"); break;
        case M_OPEN: g_path_join(path, sizeof(path), g_home(), ""); g_prompt(w, P_OPEN, "Open", "BMP file (path):", path); break;
        case M_SAVE: if (s->path[0]) { save_bmp(w, s->path); break; } /* fall through */
        case M_SAVEAS: if (s->path[0]) g_copy(path, s->path, sizeof(path)); else g_path_join(path, sizeof(path), g_home(), "picture.bmp");
                       g_prompt(w, P_SAVE, "Save As", "BMP file (path):", path); break;
        case M_UNDO: if (s->has_undo) { for (int i = 0; i < s->iw * s->ih; i++) { unsigned int t = s->image[i]; s->image[i] = s->undo[i]; s->undo[i] = t; } } break;
        case M_CLEAR: save_undo(s); for (int i = 0; i < s->iw * s->ih; i++) s->image[i] = s->secondary; s->modified = 1; break;
        case M_INVERT: save_undo(s); for (int i = 0; i < s->iw * s->ih; i++) s->image[i] ^= 0xffffff; s->modified = 1; break;
        case M_FLIPH: save_undo(s); for (int y2 = 0; y2 < s->ih; y2++) for (int x2 = 0; x2 < s->iw; x2++) AT(s, x2, y2) = s->undo[y2 * s->iw + s->iw - 1 - x2]; s->modified = 1; break;
        case M_FLIPV: save_undo(s); for (int y2 = 0; y2 < s->ih; y2++) for (int x2 = 0; x2 < s->iw; x2++) AT(s, x2, y2) = s->undo[(s->ih - 1 - y2) * s->iw + x2]; s->modified = 1; break;
        case M_EXIT: g_close(w); return;
        }
    } else if (e->type == EV_PROMPT && e->text[0]) {
        if (e->id == P_NEW) {
            int width = g_atoi(e->text), height = 0;
            for (const char *p = e->text; *p; p++) if (*p == 'x' || *p == 'X' || *p == '*') { height = g_atoi(p + 1); break; }
            new_image(s, width, height ? height : width); s->path[0] = 0;
        } else if (e->id == P_OPEN) load_bmp(w, e->text);
        else if (e->id == P_SAVE) save_bmp(w, e->text);
        else if (e->id == P_TEXT) g_copy(s->text, e->text, sizeof(s->text));
    }
}
static const GMenu menus[] = {
    {"File", {{"New...", M_NEW}, {"Open...", M_OPEN}, {"Save    Ctrl+S", M_SAVE}, {"Save As...", M_SAVEAS}, {"-", 0}, {"Exit", M_EXIT}}},
    {"Edit", {{"Undo    Ctrl+Z", M_UNDO}, {"Clear picture", M_CLEAR}}},
    {"Image", {{"Invert colors", M_INVERT}, {"Flip horizontal", M_FLIPH}, {"Flip vertical", M_FLIPV}}},
};
const GApp app_paint = {"Paint", "Draw pictures, save BMP", ICON_PAINT, 620, 400, 420, 300,
                        APP_RESIZABLE, menus, 3, sizeof(State), open, event, tick, paint, 0};
