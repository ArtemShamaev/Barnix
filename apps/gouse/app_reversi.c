/* Reversi against the computer. The computer thinks in its own time slice, so
 * the other windows keep running while it "considers" a move. */
#include "gouse.h"

enum { EMPTY, BLACK, WHITE };
enum { ID_NEW = 1, ID_PASS, M_NEW = 10, M_EASY, M_HARD };
typedef struct { unsigned char board[64]; int turn, thinking, hint, hard, last, message_shown; char status[96]; } State;
#define S ((State *)w->state)
static const signed char weights[64] = {
    100, -20, 10, 5, 5, 10, -20, 100, -20, -50, -2, -2, -2, -2, -50, -20, 10, -2, -1, -1, -1, -1, -2, 10, 5, -2, -1, -1, -1, -1, -2, 5,
    5, -2, -1, -1, -1, -1, -2, 5, 10, -2, -1, -1, -1, -1, -2, 10, -20, -50, -2, -2, -2, -2, -50, -20, 100, -20, 10, 5, 5, 10, -20, 100};
static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

static int flips(const unsigned char *b, int at, int who, int apply, unsigned char *out)
{
    if (b[at]) return 0;
    int total = 0, x0 = at % 8, y0 = at / 8, other = 3 - who;
    for (int d = 0; d < 8; d++) {
        int x = x0 + dirs[d][0], y = y0 + dirs[d][1], n = 0;
        while (x >= 0 && x < 8 && y >= 0 && y < 8 && b[y * 8 + x] == other) { x += dirs[d][0]; y += dirs[d][1]; n++; }
        if (n && x >= 0 && x < 8 && y >= 0 && y < 8 && b[y * 8 + x] == who) {
            total += n;
            if (apply) for (int k = 1; k <= n; k++) out[(y0 + dirs[d][1] * k) * 8 + x0 + dirs[d][0] * k] = who;
        }
    }
    if (apply && total) out[at] = who;
    return total;
}
static int can_move(const unsigned char *b, int who) { for (int i = 0; i < 64; i++) if (flips(b, i, who, 0, 0)) return 1; return 0; }
static void count(const unsigned char *b, int *black, int *white)
{
    *black = *white = 0;
    for (int i = 0; i < 64; i++) { if (b[i] == BLACK) (*black)++; if (b[i] == WHITE) (*white)++; }
}
static int evaluate(const unsigned char *b, int who)
{
    int score = 0, mobility = 0;
    for (int i = 0; i < 64; i++) { if (b[i] == who) score += weights[i]; else if (b[i] == 3 - who) score -= weights[i]; }
    for (int i = 0; i < 64; i++) { if (flips(b, i, who, 0, 0)) mobility++; if (flips(b, i, 3 - who, 0, 0)) mobility--; }
    return score + mobility * 4;
}
static int best_move(const unsigned char *b, int who, int hard)
{
    int best = -1, best_score = -1000000;
    for (int i = 0; i < 64; i++) {
        int n = flips(b, i, who, 0, 0);
        if (!n) continue;
        unsigned char next[64];
        for (int k = 0; k < 64; k++) next[k] = b[k];
        flips(b, i, who, 1, next);
        int score;
        if (hard) {   /* also look at the best reply */
            int worst = 1000000, any = 0;
            for (int j = 0; j < 64; j++) {
                if (!flips(next, j, 3 - who, 0, 0)) continue;
                unsigned char after[64];
                for (int k = 0; k < 64; k++) after[k] = next[k];
                flips(next, j, 3 - who, 1, after);
                int v = evaluate(after, who);
                if (v < worst) worst = v;
                any = 1;
            }
            score = any ? worst : evaluate(next, who) + 50;
        } else score = weights[i] * 2 + n + (int)(g_random() % 8);
        if (score > best_score) { best_score = score; best = i; }
    }
    return best;
}
static void finish_turn(GWindow *w)
{
    State *s = S;
    int black, white;
    count(s->board, &black, &white);
    int other = 3 - s->turn;
    if (can_move(s->board, other)) s->turn = other;
    else if (!can_move(s->board, s->turn)) {
        s->turn = EMPTY;
        g_format(s->status, sizeof(s->status), "Game over: %s  (%d : %d)", black > white ? "you win!" : black < white ? "the computer wins." : "a draw.", black, white);
        g_message_sound(w, "Reversi", s->status, black > white ? G_SOUND_WIN : black < white ? G_SOUND_ERROR : -1);
        return;
    } else g_copy(s->status, other == WHITE ? "The computer has to pass. Your move." : "You have to pass.", sizeof(s->status));
    if (s->turn == WHITE) s->thinking = 600;
    else if (s->turn == BLACK && other == BLACK) g_format(s->status, sizeof(s->status), "Your move (black).   %d : %d", black, white);
}
static void new_game(State *s)
{
    for (int i = 0; i < 64; i++) s->board[i] = EMPTY;
    s->board[27] = s->board[36] = WHITE; s->board[28] = s->board[35] = BLACK;
    s->turn = BLACK; s->thinking = 0; s->last = -1;
    g_copy(s->status, "Your move (black). Click a square that turns over white discs.", sizeof(s->status));
}
static void open(GWindow *w, const char *argument) { (void)argument; new_game(S); S->hint = 1; }
static void tick(GWindow *w, int dt)
{
    State *s = S;
    if (s->turn != WHITE || s->thinking <= 0) return;
    s->thinking -= dt;
    if (s->thinking > 0) return;
    int move = best_move(s->board, WHITE, s->hard);
    if (move >= 0) { flips(s->board, move, WHITE, 1, s->board); s->last = move; g_sound(G_SOUND_DISC); }
    s->turn = WHITE;
    finish_turn(w);
    g_invalidate(w);
}
static void geometry(GWindow *w, int *x, int *y, int *cell)
{
    int size = (w->w < w->h - 70 ? w->w : w->h - 70) - 16;
    *cell = size / 8; *x = (w->w - *cell * 8) / 2; *y = 8;
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    int bx, by, cell; geometry(w, &bx, &by, &cell);
    g_panel(c, bx - 4, by - 4, cell * 8 + 8, cell * 8 + 8, 0x1d6e35, 0);
    for (int i = 0; i < 64; i++) {
        int x = bx + i % 8 * cell, y = by + i / 8 * cell;
        bgx_fill(c, x, y, cell - 1, cell - 1, (i + i / 8) % 2 ? 0x2a9a4a : 0x2f9e4f);
        if (s->board[i]) {
            int r = cell / 2 - 4;
            g_disc(c, x + cell / 2 + 1, y + cell / 2 + 2, r, 0x14501f);
            g_disc(c, x + cell / 2, y + cell / 2, r, s->board[i] == BLACK ? 0x16161c : 0xf4f4f4);
            g_disc(c, x + cell / 2 - r / 3, y + cell / 2 - r / 3, r / 4, s->board[i] == BLACK ? 0x50505a : 0xffffff);
            if (i == s->last) g_disc(c, x + cell / 2, y + cell / 2, 3, 0xe23d48);
        } else if (s->hint && s->turn == BLACK && flips(s->board, i, BLACK, 0, 0)) g_disc(c, x + cell / 2, y + cell / 2, 3, 0x1d6e35);
    }
    int black, white; count(s->board, &black, &white);
    int y = by + cell * 8 + 10;
    g_disc(c, 18, y + 12, 8, 0x16161c); char t[16]; g_format(t, sizeof(t), "%d", black); g_text(c, 30, y + 4, t, g_theme.text);
    g_disc(c, 74, y + 12, 8, 0xf4f4f4); g_ring(c, 74, y + 12, 8, 0x404040); g_format(t, sizeof(t), "%d", white); g_text(c, 86, y + 4, t, g_theme.text);
    g_button(w, c, ID_NEW, w->w - 100, y, 92, 24, "New game");
    g_text_fit(c, 8, y + 30, w->w - 16, s->turn == WHITE ? "The computer is thinking..." : s->status, g_theme.text);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_MOUSE_DOWN && e->button == 1 && s->turn == BLACK) {
        int bx, by, cell; geometry(w, &bx, &by, &cell);
        int x = (e->x - bx) / cell, y = (e->y - by) / cell;
        if (e->x < bx || e->y < by || x > 7 || y > 7) return;
        if (!flips(s->board, y * 8 + x, BLACK, 1, s->board)) return;
        s->last = y * 8 + x;
        g_sound(G_SOUND_DISC);
        finish_turn(w);
    } else if (e->type == EV_COMMAND && e->id == ID_NEW) new_game(s);
    else if (e->type == EV_MENU) {
        if (e->id == M_NEW) new_game(s);
        else if (e->id == M_EASY) s->hard = 0;
        else if (e->id == M_HARD) s->hard = 1;
        else if (e->id == M_HARD + 1) s->hint = !s->hint;
    }
}
static const GMenu menus[] = {{"Game", {{"New game", M_NEW}, {"-", 0}, {"Beginner", M_EASY}, {"Expert", M_HARD}, {"Show moves", M_HARD + 1}}}};
const GApp app_reversi = {"Reversi", "The classic disc game", ICON_REVERSI, 360, 420, 260, 330,
                          APP_RESIZABLE, menus, 1, sizeof(State), open, event, tick, paint, 0};
