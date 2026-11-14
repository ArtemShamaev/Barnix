/* Multi-line UTF-8 editor: cursor, selection, clipboard, scrolling, mouse.
 * Every code point is one 8x16 cell; lines end with '\n'. */
#include "gouse.h"

#define CELL_W 8
#define CELL_H 16

static int line_start(const GTextEdit *t, int pos) { while (pos > 0 && t->buffer[pos - 1] != '\n') pos--; return pos; }
static int line_end(const GTextEdit *t, int pos) { while (pos < t->length && t->buffer[pos] != '\n') pos++; return pos; }
static int column_of(const GTextEdit *t, int pos)
{
    int col = 0;
    for (int i = line_start(t, pos); i < pos; i = g_utf8_next(t->buffer, t->length, i)) col++;
    return col;
}
static int at_column(const GTextEdit *t, int start, int col)
{
    int pos = start;
    while (col-- > 0 && pos < t->length && t->buffer[pos] != '\n') pos = g_utf8_next(t->buffer, t->length, pos);
    return pos;
}
static int line_index(const GTextEdit *t, int pos)
{
    int n = 0;
    for (int i = 0; i < pos; i++) if (t->buffer[i] == '\n') n++;
    return n;
}
static int line_count(const GTextEdit *t) { return line_index(t, t->length) + 1; }
static int start_of_line(const GTextEdit *t, int line)
{
    int pos = 0;
    while (line > 0 && pos < t->length) { if (t->buffer[pos] == '\n') line--; pos++; }
    return pos;
}

void gte_init(GTextEdit *t, char *buffer, int capacity)
{
    t->buffer = buffer; t->capacity = capacity;
    t->length = t->cursor = t->top = t->left = t->goal = t->dragging = t->modified = 0;
    t->anchor = t->shown = -1;
    t->highlight = 0; t->gutter = 0; t->palette = 0; t->mark[0] = t->mark[1] = -1;
    t->dots = 0; t->dot_count = 0; t->stop_line = 0;
    if (capacity) buffer[0] = 0;
}
int gte_line_of(const GTextEdit *t, int pos) { return line_index(t, pos); }
int gte_line_start(const GTextEdit *t, int line) { return start_of_line(t, line); }
void gte_goto_line(GTextEdit *t, int line)
{
    int total = line_count(t);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    t->cursor = start_of_line(t, line); t->anchor = -1; t->goal = 0; t->shown = -1;
}
static int gutter_width(const GTextEdit *t)
{
    if (!t->gutter) return 0;
    int digits = 1;
    for (int n = line_count(t); n >= 10; n /= 10) digits++;
    if (digits < 3) digits = 3;
    return digits * CELL_W + 8 + (t->dots ? 14 : 0);
}
int gte_gutter_width(const GTextEdit *t) { return gutter_width(t); }
void gte_set(GTextEdit *t, const char *text, int length)
{
    int n = 0;
    for (int i = 0; i < length && n + 1 < t->capacity; i++)
        if (text[i] != '\r') t->buffer[n++] = text[i] == '\t' ? ' ' : text[i];
    t->buffer[n] = 0;
    t->length = n; t->cursor = t->top = t->left = 0; t->anchor = t->shown = -1; t->modified = 0;
}
int gte_selection(const GTextEdit *t, int *from, int *to)
{
    if (t->anchor < 0 || t->anchor == t->cursor) return 0;
    *from = t->anchor < t->cursor ? t->anchor : t->cursor;
    *to = t->anchor < t->cursor ? t->cursor : t->anchor;
    return 1;
}
static void remove_range(GTextEdit *t, int from, int to)
{
    for (int i = to; i <= t->length; i++) t->buffer[from + i - to] = t->buffer[i];
    t->length -= to - from;
    t->cursor = from; t->anchor = -1; t->modified = 1;
}
static int delete_selection(GTextEdit *t)
{
    int from, to;
    if (!gte_selection(t, &from, &to)) { t->anchor = -1; return 0; }
    remove_range(t, from, to);
    return 1;
}
void gte_insert(GTextEdit *t, const char *text, int length)
{
    delete_selection(t);
    int n = 0;
    for (int i = 0; i < length; i++) if (text[i] != '\r') n++;
    if (t->length + n >= t->capacity) n = t->capacity - 1 - t->length;
    if (n <= 0) return;
    for (int i = t->length; i >= t->cursor; i--) t->buffer[i + n] = t->buffer[i];
    for (int i = 0, k = 0; k < n; i++) if (text[i] != '\r') t->buffer[t->cursor + k++] = text[i] == '\t' ? ' ' : text[i];
    t->length += n; t->cursor += n; t->modified = 1;
}
void gte_copy(GTextEdit *t, int cut)
{
    int from, to;
    if (!gte_selection(t, &from, &to)) return;
    g_clipboard_set(t->buffer + from, to - from);
    if (cut) remove_range(t, from, to);
}
static void move(GTextEdit *t, int pos, int keep_goal)
{
    if (g_shift()) { if (t->anchor < 0) t->anchor = t->cursor; }
    else t->anchor = -1;
    t->cursor = pos < 0 ? 0 : pos > t->length ? t->length : pos;
    if (!keep_goal) t->goal = column_of(t, t->cursor);
}
static void vertical(GTextEdit *t, int lines)
{
    int line = line_index(t, t->cursor) + lines, total = line_count(t);
    if (line < 0) line = 0;
    if (line >= total) line = total - 1;
    move(t, at_column(t, start_of_line(t, line), t->goal), 1);
}
int gte_key(GTextEdit *t, const GEvent *e, int rows)
{
    if (e->type != EV_KEY) return 0;
    switch (e->code) {
    case BGX_KEY_LEFT: move(t, g_utf8_prev(t->buffer, t->cursor), 0); return 1;
    case BGX_KEY_RIGHT: move(t, g_utf8_next(t->buffer, t->length, t->cursor), 0); return 1;
    case BGX_KEY_UP: vertical(t, -1); return 1;
    case BGX_KEY_DOWN: vertical(t, 1); return 1;
    case 0xc9: vertical(t, -(rows > 1 ? rows - 1 : 1)); return 1;   /* Page Up */
    case 0xd1: vertical(t, rows > 1 ? rows - 1 : 1); return 1;      /* Page Down */
    case 0xc7: move(t, g_ctrl() ? 0 : line_start(t, t->cursor), 0); return 1;
    case 0xcf: move(t, g_ctrl() ? t->length : line_end(t, t->cursor), 0); return 1;
    case 0xd3:   /* Delete */
        if (!delete_selection(t) && t->cursor < t->length) remove_range(t, t->cursor, g_utf8_next(t->buffer, t->length, t->cursor));
        t->goal = column_of(t, t->cursor);
        return 1;
    }
    unsigned int k = e->key;
    if (k == 1) { t->anchor = 0; t->cursor = t->length; return 1; }          /* Ctrl+A */
    if (k == 3) { gte_copy(t, 0); return 1; }                                 /* Ctrl+C */
    if (k == 24) { gte_copy(t, 1); return 1; }                                /* Ctrl+X */
    if (k == 22) { gte_insert(t, g_clipboard, g_clipboard_len); t->goal = column_of(t, t->cursor); return 1; }
    if (k == '\b') {
        if (!delete_selection(t) && t->cursor > 0) remove_range(t, g_utf8_prev(t->buffer, t->cursor), t->cursor);
        t->goal = column_of(t, t->cursor);
        return 1;
    }
    if (k == '\n' || k == '\t' || (k >= 32 && k != 127 && !(k >= 0x100 && k <= 0x108))) {
        char bytes[4]; int n = k == '\t' ? 4 : g_utf8_put(bytes, k);
        if (k == '\t') bytes[0] = bytes[1] = bytes[2] = bytes[3] = ' ';
        gte_insert(t, bytes, n);
        t->goal = column_of(t, t->cursor);
        return 1;
    }
    return 0;
}
static int position_at(GTextEdit *t, int x, int y, int ox, int oy)
{
    ox += gutter_width(t);
    int line = t->top + (y - oy - 2) / CELL_H, col = t->left + (x - ox - 4 + CELL_W / 2) / CELL_W;
    if (y - oy - 2 < 0) line = t->top - 1;
    if (line < 0) line = 0;
    int total = line_count(t);
    if (line >= total) return t->length;
    return at_column(t, start_of_line(t, line), col < 0 ? 0 : col);
}
void gte_mouse(GTextEdit *t, const GEvent *e, int x, int y, int w, int h)
{
    int inside = e->x >= x && e->y >= y && e->x < x + w && e->y < y + h;
    if (e->type == EV_MOUSE_DOWN && e->button == 1 && inside) {
        t->cursor = position_at(t, e->x, e->y, x, y);
        t->anchor = t->cursor; t->dragging = 1; t->goal = column_of(t, t->cursor);
    } else if (e->type == EV_MOUSE_MOVE && t->dragging) {
        t->cursor = position_at(t, e->x, e->y, x, y);
        t->goal = column_of(t, t->cursor);
    } else if (e->type == EV_MOUSE_UP) {
        t->dragging = 0;
        if (t->anchor == t->cursor) t->anchor = -1;
    } else if (e->type == EV_WHEEL && inside) {
        t->top += e->id * 3;
        int total = line_count(t);
        if (t->top > total - 1) t->top = total - 1;
        if (t->top < 0) t->top = 0;
    } else if (e->type == EV_DOUBLE && inside) {   /* select a word */
        int a = position_at(t, e->x, e->y, x, y), b = a;
        while (a > 0 && t->buffer[a - 1] != ' ' && t->buffer[a - 1] != '\n') a--;
        while (b < t->length && t->buffer[b] != ' ' && t->buffer[b] != '\n') b++;
        t->anchor = a; t->cursor = b; t->dragging = 0;
    }
}
static const unsigned int syntax_colors[] = {0, 0x0b3fb0, 0x007a7a, 0xa31515, 0x2e8b2e, 0x9a4f00, 0x8a2be2, 0x795e26};
void gte_draw(GTextEdit *t, BgxCanvas *c, int x, int y, int w, int h, int focused)
{
    const unsigned int *p = t->palette;
    unsigned int back = p ? p[GTE_PAL_BACK] : g_theme.window, text = p ? p[GTE_PAL_TEXT] : g_theme.text;
    unsigned int selection = p ? p[GTE_PAL_SELECTION] : focused ? g_theme.highlight : g_theme.shadow;
    unsigned int selected_text = p ? text : g_theme.highlight_text;
    int gutter = gutter_width(t);
    bgx_fill(c, x, y, w, h, back);
    if (gutter) { bgx_fill(c, x, y, gutter - 3, h, p ? p[GTE_PAL_GUTTER] : g_theme.face); x += gutter; w -= gutter; }
    int rows = (h - 4) / CELL_H, cols = (w - 8) / CELL_W;
    /* Keep the cursor on screen (unless the wheel moved the view). */
    int cursor_line = line_index(t, t->cursor), cursor_col = column_of(t, t->cursor);
    if (t->cursor != t->shown || t->dragging) {
        if (cursor_line < t->top) t->top = cursor_line;
        if (cursor_line >= t->top + rows) t->top = cursor_line - rows + 1;
        if (cursor_col < t->left) t->left = cursor_col;
        if (cursor_col >= t->left + cols) t->left = cursor_col - cols + 1;
        t->shown = t->cursor;
    }
    int from = 0, to = 0, selected = gte_selection(t, &from, &to);
    if (p && !selected && cursor_line >= t->top && cursor_line < t->top + rows)   /* the current line */
        bgx_fill(c, x, y + 2 + (cursor_line - t->top) * CELL_H, w, CELL_H, p[GTE_PAL_LINE]);
    /* syntax state at the first visible line */
    static unsigned char classes[4096];
    int state = 0;
    if (t->highlight)
        for (int q = 0, line = 0; line < t->top && q < t->length; line++) {
            int e = line_end(t, q), n = e - q < (int)sizeof(classes) ? e - q : (int)sizeof(classes);
            t->highlight(t->buffer + q, n, classes, &state);
            q = e + 1;
        }
    int pos = start_of_line(t, t->top);
    for (int r = 0; r < rows && pos <= t->length; r++) {
        int col = 0, py = y + 2 + r * CELL_H, begin = pos;
        if (gutter) {
            char number[12]; g_format(number, sizeof(number), "%d", t->top + r + 1);
            int nw = g_len(number) * CELL_W;
            unsigned int color = t->top + r == cursor_line ? (p ? p[GTE_PAL_NUMBER_CURRENT] : g_theme.text) : (p ? p[GTE_PAL_NUMBER] : g_theme.shadow);
            g_text_raw(c, x - 8 - nw, py, number, color);
            for (int k = 0; k < t->dot_count; k++)
                if (t->dots[k] == t->top + r + 1) { g_disc(c, x - gutter + 8, py + 8, 5, 0xe51400); break; }
            if (t->stop_line == t->top + r + 1) {   /* the debugger stopped here: a yellow arrow */
                for (int k = 0; k < 6; k++) g_line(c, x - gutter + 3 + k, py + 3 + k, x - gutter + 3 + k, py + 13 - k, 0xffcc00);
            }
        }
        if (t->stop_line == t->top + r + 1) bgx_blend(c, x, py, w, CELL_H, 0xffcc00, 40);
        int end = line_end(t, pos);
        if (t->highlight) {
            int n = end - pos < (int)sizeof(classes) ? end - pos : (int)sizeof(classes);
            for (int i = 0; i < n; i++) classes[i] = 0;
            t->highlight(t->buffer + pos, n, classes, &state);
        }
        while (pos < t->length && t->buffer[pos] != '\n') {
            int next = g_utf8_next(t->buffer, t->length, pos);
            int cx = x + 4 + (col - t->left) * CELL_W;
            if (col >= t->left && col < t->left + cols) {
                char glyph[5]; int n = next - pos;
                for (int i = 0; i < n && i < 4; i++) glyph[i] = t->buffer[pos + i];
                glyph[n < 4 ? n : 4] = 0;
                int in_selection = selected && pos >= from && pos < to;
                unsigned int color = text;
                if (t->highlight && pos - begin < (int)sizeof(classes) && classes[pos - begin]) {
                    int k = classes[pos - begin] % 8;
                    color = p ? p[GTE_PAL_SYNTAX + k - 1] : syntax_colors[k];
                }
                if (in_selection) bgx_fill(c, cx, py, CELL_W, CELL_H, selection);
                if (pos == t->mark[0] || pos == t->mark[1]) bgx_frame(c, cx, py, CELL_W, CELL_H, 1, p ? p[GTE_PAL_MARK] : g_theme.shadow);
                g_text_raw(c, cx, py, glyph, in_selection ? selected_text : color);
            }
            col++; pos = next;
        }
        if (selected && pos >= from && pos < to && col >= t->left && col < t->left + cols)   /* selected line break */
            bgx_fill(c, x + 4 + (col - t->left) * CELL_W, py, CELL_W / 2, CELL_H, selection);
        pos++;
    }
    if (focused && (g_now() / 500) % 2 == 0 && cursor_line >= t->top && cursor_line < t->top + rows &&
        cursor_col >= t->left && cursor_col <= t->left + cols)
        bgx_fill(c, x + 4 + (cursor_col - t->left) * CELL_W, y + 2 + (cursor_line - t->top) * CELL_H, 2, CELL_H, p ? p[GTE_PAL_CURSOR] : g_theme.text);
}
