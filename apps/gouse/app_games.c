/* Two small games: Mines (find the mines) and Puzzle (the fifteen puzzle). */
#include "gouse.h"

/* ---------------------------------------------------------------- Mines */
#define MAX_SIDE 16
enum { ID_FACE = 1, M_BEGINNER = 10, M_INTERMEDIATE, M_EXPERT };
typedef struct {
    int w, h, mines, flags, opened, state, elapsed, started;   /* state 0 playing, 1 lost, 2 won */
    unsigned char mine[MAX_SIDE * 30], open[MAX_SIDE * 30], flag[MAX_SIDE * 30], near[MAX_SIDE * 30];
    int seeded, boom;
} Mines;
#define M ((Mines *)w->state)
#define CELL 22

static void mines_new(Mines *m, int width, int height, int count)
{
    m->w = width; m->h = height; m->mines = count;
    m->flags = m->opened = m->state = m->elapsed = m->started = m->seeded = 0; m->boom = -1;
    for (int i = 0; i < width * height; i++) m->mine[i] = m->open[i] = m->flag[i] = m->near[i] = 0;
}
static void mines_seed(Mines *m, int safe)
{
    for (int placed = 0; placed < m->mines;) {
        int i = (int)(g_random() % (unsigned int)(m->w * m->h));
        int dx = i % m->w - safe % m->w, dy = i / m->w - safe / m->w;
        if (m->mine[i] || (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1)) continue;
        m->mine[i] = 1; placed++;
    }
    for (int i = 0; i < m->w * m->h; i++) {
        int n = 0, x = i % m->w, y = i / m->w;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && m->mine[ny * m->w + nx]) n++;
        }
        m->near[i] = n;
    }
    m->seeded = 1; m->started = 1;
}
static void mines_reveal(Mines *m, int start)
{
    static int stack[MAX_SIDE * 30 * 8 + 8];
    int top = 0;
    stack[top++] = start;
    while (top) {
        int i = stack[--top];
        if (m->open[i] || m->flag[i]) continue;
        m->open[i] = 1; m->opened++;
        if (m->near[i] || m->mine[i]) continue;
        int x = i % m->w, y = i / m->w;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && !m->open[ny * m->w + nx] && top < MAX_SIDE * 30 * 8) stack[top++] = ny * m->w + nx;
        }
    }
}
static void mines_open(GWindow *w, const char *argument) { (void)argument; mines_new(M, 9, 9, 10); }
static void mines_origin(GWindow *w, int *x, int *y) { *x = (w->w - M->w * CELL) / 2; *y = 48; }
static void mines_paint(GWindow *w, BgxCanvas *c)
{
    Mines *m = M;
    static const unsigned int colors[9] = {0, 0x1030d0, 0x108020, 0xd02020, 0x101080, 0x801010, 0x108080, 0x101010, 0x808080};
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    char text[16];
    g_panel(c, 8, 8, 56, 30, 0x200000, 1); g_format(text, sizeof(text), "%03d", m->mines - m->flags);
    bgx_text(c, 12, 15, text, 0xff3030, 1); bgx_text(c, 13, 15, text, 0xff3030, 1);
    g_panel(c, w->w - 64, 8, 56, 30, 0x200000, 1); g_format(text, sizeof(text), "%03d", m->elapsed / 1000 > 999 ? 999 : m->elapsed / 1000);
    bgx_text(c, w->w - 60, 15, text, 0xff3030, 1); bgx_text(c, w->w - 59, 15, text, 0xff3030, 1);
    int fx = w->w / 2 - 15;
    g_panel(c, fx, 8, 30, 30, g_theme.face, w->pressed_zone == ID_FACE);
    g_disc(c, fx + 15, 23, 10, 0xffd84a); g_ring(c, fx + 15, 23, 10, 0x202020);
    if (m->state == 1) { g_line(c, fx + 9, 18, fx + 13, 22, 0); g_line(c, fx + 13, 18, fx + 9, 22, 0); g_line(c, fx + 17, 18, fx + 21, 22, 0); g_line(c, fx + 21, 18, fx + 17, 22, 0); }
    else if (m->state == 2) { bgx_fill(c, fx + 7, 18, 16, 3, 0); }
    else { bgx_fill(c, fx + 10, 19, 3, 3, 0); bgx_fill(c, fx + 17, 19, 3, 3, 0); }
    if (m->state == 1) bgx_fill(c, fx + 11, 27, 8, 2, 0);
    else for (int k = 0; k < 9; k++) bgx_fill(c, fx + 11 + k, 27 + (k == 0 || k == 8 ? -1 : 0), 1, 1, 0);
    g_zone(w, ID_FACE, fx, 8, 30, 30);
    int ox, oy; mines_origin(w, &ox, &oy);
    g_bevel(c, ox - 3, oy - 3, m->w * CELL + 6, m->h * CELL + 6, 1);
    for (int i = 0; i < m->w * m->h; i++) {
        int x = ox + i % m->w * CELL, y = oy + i / m->w * CELL;
        int shown = m->open[i] || (m->state == 1 && m->mine[i]);
        if (!shown) {
            g_panel(c, x, y, CELL, CELL, g_theme.face, 0);
            if (m->flag[i]) {
                bgx_fill(c, x + 10, y + 5, 2, 12, 0); bgx_fill(c, x + 6, y + 16, 10, 2, 0);
                for (int k = 0; k < 6; k++) bgx_fill(c, x + 4 + k, y + 5 + k / 2, 1, 6 - k, 0xe02020);
            }
            continue;
        }
        bgx_fill(c, x, y, CELL, CELL, i == m->boom ? 0xff4040 : 0xd8dce4);
        bgx_frame(c, x, y, CELL, CELL, 1, 0xa0a4ac);
        if (m->mine[i]) { g_disc(c, x + 11, y + 11, 5, 0x101010); bgx_fill(c, x + 4, y + 10, 15, 2, 0x101010); bgx_fill(c, x + 10, y + 4, 2, 15, 0x101010); bgx_fill(c, x + 9, y + 8, 2, 2, 0xffffff); }
        else if (m->near[i]) { char n[2] = {(char)('0' + m->near[i]), 0}; bgx_text(c, x + 7, y + 3, n, colors[m->near[i]], 1); bgx_text(c, x + 8, y + 3, n, colors[m->near[i]], 1); }
    }
}
static void mines_event(GWindow *w, GEvent *e)
{
    Mines *m = M;
    if (e->type == EV_COMMAND && e->id == ID_FACE) { mines_new(m, m->w, m->h, m->mines); return; }
    if (e->type == EV_MENU) {
        if (e->id == M_BEGINNER) mines_new(m, 9, 9, 10);
        else if (e->id == M_INTERMEDIATE) mines_new(m, 16, 16, 40);
        else if (e->id == M_EXPERT) mines_new(m, 30, 16, 99);
        int width = m->w * CELL + 40, height = m->h * CELL + 70;
        if (w->w < width || w->h < height) g_message(w, "Mines", "Make the window larger to see the whole field.");
        return;
    }
    if (e->type != EV_MOUSE_DOWN || m->state) return;
    int ox, oy; mines_origin(w, &ox, &oy);
    int x = (e->x - ox) / CELL, y = (e->y - oy) / CELL;
    if (e->x < ox || e->y < oy || x >= m->w || y >= m->h) return;
    int i = y * m->w + x;
    if (e->button == 2) {
        if (!m->open[i]) { m->flag[i] = !m->flag[i]; m->flags += m->flag[i] ? 1 : -1; g_sound(G_SOUND_FLAG); }
        return;
    }
    if (m->flag[i]) return;
    if (!m->seeded) mines_seed(m, i);
    if (m->open[i] && m->near[i]) {   /* chord: open neighbours when the flags match */
        int flags = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < m->w && ny < m->h && m->flag[ny * m->w + nx]) flags++;
        }
        if (flags != m->near[i]) return;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy, j = ny * m->w + nx;
            if (nx < 0 || ny < 0 || nx >= m->w || ny >= m->h || m->flag[j] || m->open[j]) continue;
            if (m->mine[j]) { m->state = 1; m->boom = j; }
            else mines_reveal(m, j);
        }
    } else if (m->mine[i]) { m->state = 1; m->boom = i; }
    else mines_reveal(m, i);
    g_sound(m->state == 1 ? G_SOUND_BOOM : G_SOUND_REVEAL);
    if (!m->state && m->opened == m->w * m->h - m->mines) {
        m->state = 2;
        char text[64]; g_format(text, sizeof(text), "You found all the mines in %d seconds!", m->elapsed / 1000);
        g_message_sound(w, "Mines", text, G_SOUND_WIN);
    }
}
static void mines_tick(GWindow *w, int dt)
{
    Mines *m = M;
    if (!m->started || m->state) return;
    int before = m->elapsed / 1000;
    m->elapsed += dt;
    if (m->elapsed / 1000 != before) g_invalidate(w);
}
static const GMenu mines_menus[] = {{"Game", {{"Beginner (9x9)", M_BEGINNER}, {"Intermediate (16x16)", M_INTERMEDIATE}, {"Expert (30x16)", M_EXPERT}}}};
const GApp app_mines = {"Mines", "Clear the field without a bang", ICON_MINES, 9 * CELL + 40, 9 * CELL + 70, 9 * CELL + 30, 9 * CELL + 60,
                        APP_RESIZABLE, mines_menus, 1, sizeof(Mines), mines_open, mines_event, mines_tick, mines_paint, 0};

/* ---------------------------------------------------------------- Puzzle */
enum { ID_SHUFFLE = 1 };
typedef struct { unsigned char tile[16]; int moves, solved; } Puzzle;
#define P ((Puzzle *)w->state)
static int puzzle_slide(Puzzle *p, int i)
{
    int blank = 0;
    while (p->tile[blank]) blank++;
    int dx = i % 4 - blank % 4, dy = i / 4 - blank / 4;
    if (dx && dy) return 0;
    int step = dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 4 : -4);
    if (!dx && !dy) return 0;
    while (blank != i) { p->tile[blank] = p->tile[blank + step]; blank += step; }   /* slide a whole row/column */
    p->tile[i] = 0;
    return 1;
}
static void puzzle_shuffle(Puzzle *p)
{
    for (int i = 0; i < 16; i++) p->tile[i] = (unsigned char)((i + 1) % 16);
    for (int k = 0; k < 400; k++) puzzle_slide(p, (int)(g_random() % 16));   /* only legal moves: always solvable */
    p->moves = 0; p->solved = 0;
}
static void puzzle_open(GWindow *w, const char *argument) { (void)argument; puzzle_shuffle(P); }
static void puzzle_paint(GWindow *w, BgxCanvas *c)
{
    Puzzle *p = P;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    int size = (w->w < w->h - 50 ? w->w : w->h - 50) - 20, cell = size / 4, ox = (w->w - cell * 4) / 2, oy = 10;
    g_panel(c, ox - 4, oy - 4, cell * 4 + 8, cell * 4 + 8, 0x404858, 1);
    static const unsigned int colors[4] = {0xe23d48, 0xf0b020, 0x3b7ddd, 0x3caa50};
    for (int i = 0; i < 16; i++) {
        if (!p->tile[i]) continue;
        int x = ox + i % 4 * cell, y = oy + i / 4 * cell, t = p->tile[i];
        unsigned int color = colors[(t - 1) / 4];
        g_panel(c, x + 2, y + 2, cell - 4, cell - 4, color, 0);
        char n[4]; g_format(n, sizeof(n), "%d", t);
        int scale = cell >= 64 ? 3 : cell >= 40 ? 2 : 1;
        bgx_text_shadow(c, x + (cell - bgx_text_width(n, scale)) / 2, y + (cell - 16 * scale) / 2, n, 0xffffff, scale);
    }
    char text[64];
    g_format(text, sizeof(text), p->solved ? "Solved in %d moves!" : "Moves: %d", p->moves);
    g_text(c, 10, w->h - 32, text, g_theme.text);
    g_button(w, c, ID_SHUFFLE, w->w - 100, w->h - 36, 90, 26, "Shuffle");
}
static void puzzle_event(GWindow *w, GEvent *e)
{
    Puzzle *p = P;
    if (e->type == EV_COMMAND && e->id == ID_SHUFFLE) { puzzle_shuffle(p); return; }
    int blank = 0; while (p->tile[blank]) blank++;
    int target = -1;
    if (e->type == EV_MOUSE_DOWN && !p->solved) {
        int size = (w->w < w->h - 50 ? w->w : w->h - 50) - 20, cell = size / 4, ox = (w->w - cell * 4) / 2, oy = 10;
        int x = (e->x - ox) / cell, y = (e->y - oy) / cell;
        if (e->x >= ox && e->y >= oy && x < 4 && y < 4) target = y * 4 + x;
    } else if (e->type == EV_KEY && !p->solved) {   /* arrows move the tile next to the gap */
        if (e->code == BGX_KEY_LEFT && blank % 4 < 3) target = blank + 1;
        if (e->code == BGX_KEY_RIGHT && blank % 4 > 0) target = blank - 1;
        if (e->code == BGX_KEY_UP && blank < 12) target = blank + 4;
        if (e->code == BGX_KEY_DOWN && blank >= 4) target = blank - 4;
    }
    if (target >= 0 && puzzle_slide(p, target)) {
        p->moves++;
        g_sound(G_SOUND_SLIDE);
        int ok = 1;
        for (int i = 0; i < 15; i++) if (p->tile[i] != i + 1) ok = 0;
        if (ok) { p->solved = 1; char text[64]; g_format(text, sizeof(text), "Well done! Solved in %d moves.", p->moves); g_message_sound(w, "Puzzle", text, G_SOUND_WIN); }
    }
}
const GApp app_puzzle = {"Puzzle", "Put the fifteen tiles in order", ICON_PUZZLE, 300, 340, 200, 240,
                         APP_RESIZABLE, 0, 0, sizeof(Puzzle), puzzle_open, puzzle_event, 0, puzzle_paint, 0};
