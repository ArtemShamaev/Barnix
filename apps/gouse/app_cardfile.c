/* Cardfile: index cards with a title and a body, sorted by title. */
#include "gouse.h"

#define MAX_CARDS 64
#define BODY_MAX 2048
enum { M_NEW = 1, M_DELETE, M_SAVE, M_OPEN, M_SAVEAS, M_FIND, P_OPEN = 50, P_SAVE, P_FIND, C_DELETE, ID_ADD = 80, ID_DEL };
typedef struct { char title[64]; char body[BODY_MAX]; } Card;
typedef struct {
    Card cards[MAX_CARDS];
    int count, selected, first, focus;   /* focus: 0 list, 1 title, 2 body */
    GEdit title;
    char body[BODY_MAX];
    GTextEdit edit;
    char path[256];
    int modified;
} State;
#define S ((State *)w->state)

static void store(State *s)
{
    if (s->selected < 0 || s->selected >= s->count) return;
    Card *card = &s->cards[s->selected];
    if (!g_equal(card->title, s->title.text) || s->edit.modified) s->modified = 1;
    g_copy(card->title, s->title.text, sizeof(card->title));
    for (int i = 0; i <= s->edit.length; i++) card->body[i] = s->body[i];
    s->edit.modified = 0;
}
static void select_card(State *s, int index)
{
    store(s);
    s->selected = index;
    if (index < 0 || index >= s->count) { g_edit_set(&s->title, ""); gte_set(&s->edit, "", 0); return; }
    g_edit_set(&s->title, s->cards[index].title);
    gte_set(&s->edit, s->cards[index].body, g_len(s->cards[index].body));
}
static int title_less(const char *a, const char *b)
{
    while (*a && (*a | 32) == (*b | 32)) { a++; b++; }
    return (*a | 32) < (*b | 32);
}
static void sort(State *s)
{
    store(s);
    Card *keep = s->selected >= 0 ? &s->cards[s->selected] : 0;
    char wanted[64]; g_copy(wanted, keep ? keep->title : "", sizeof(wanted));
    static Card temp;
    for (int i = 1; i < s->count; i++) {
        temp = s->cards[i]; int j = i;
        while (j > 0 && title_less(temp.title, s->cards[j - 1].title)) { s->cards[j] = s->cards[j - 1]; j--; }
        s->cards[j] = temp;
    }
    for (int i = 0; i < s->count; i++) if (g_equal(s->cards[i].title, wanted)) s->selected = i;
}
static void update_title(GWindow *w)
{
    char title[300]; const char *name = S->path;
    for (const char *p = S->path; *p; p++) if (*p == '/') name = p + 1;
    g_format(title, sizeof(title), "Cardfile - %s%s", name, S->modified ? " *" : "");
    if (!g_equal(title, w->title)) g_set_title(w, title);
}
static void load(GWindow *w, const char *path)
{
    State *s = S;
    static char data[MAX_CARDS * (BODY_MAX + 80)];
    int n = g_read_file(path, data, sizeof(data) - 1);
    g_copy(s->path, path, sizeof(s->path));
    s->count = 0; s->selected = -1;
    if (n > 0) {
        data[n] = 0;
        Card *card = 0; int body = 0;
        for (char *line = data; *line;) {
            char *end = line; while (*end && *end != '\n') end++;
            char saved = *end; *end = 0;
            int len = (int)(end - line);
            if (len >= 6 && line[0] == '=' && line[1] == '=' && line[2] == ' ' && line[len - 1] == '=' && line[len - 2] == '=' && s->count < MAX_CARDS) {
                card = &s->cards[s->count++]; body = 0; card->body[0] = 0;
                line[len - 3] = 0; g_copy(card->title, line + 3, sizeof(card->title));
            } else if (card) {
                const char *text = line[0] == '\\' ? line + 1 : line;
                for (int i = 0; text[i] && body < BODY_MAX - 2; i++) card->body[body++] = text[i];
                if (body < BODY_MAX - 1) card->body[body++] = '\n';
                card->body[body] = 0;
            }
            *end = saved; line = *end ? end + 1 : end;
        }
        for (int i = 0; i < s->count; i++) {   /* drop the newline the format adds */
            int len = g_len(s->cards[i].body);
            if (len && s->cards[i].body[len - 1] == '\n') s->cards[i].body[len - 1] = 0;
        }
    } else if (n < 0) {
        g_copy(s->cards[0].title, g_tr("Welcome to Cardfile"), sizeof(s->cards[0].title));
        g_copy(s->cards[0].body, g_tr("Each card has a title and some text.\nAdd cards with Card > New or the Add button.\nCards are kept sorted by title."), BODY_MAX);
        s->count = 1;
    }
    s->modified = 0;
    s->selected = -1;
    select_card(s, s->count ? 0 : -1);
    update_title(w);
}
static void save(GWindow *w, const char *path)
{
    State *s = S;
    store(s);
    static char data[MAX_CARDS * (BODY_MAX + 80)];
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        n += g_format(data + n, sizeof(data) - n, "== %s ==\n", s->cards[i].title);
        const char *b = s->cards[i].body;
        while (*b) {
            if (*b == '\\' || (b[0] == '=' && b[1] == '=')) data[n++] = '\\';
            while (*b && *b != '\n') data[n++] = *b++;
            data[n++] = '\n';
            if (*b) b++;
        }
    }
    if (g_write_file(path, data, n)) { g_error(w, "Cardfile", "Cannot save the cards there."); return; }
    g_copy(s->path, path, sizeof(s->path));
    s->modified = 0;
    update_title(w);
}
static void add(GWindow *w)
{
    State *s = S;
    if (s->count >= MAX_CARDS) { g_error(w, "Cardfile", "The card file is full (64 cards)."); return; }
    store(s);
    g_copy(s->cards[s->count].title, "New card", sizeof(s->cards[0].title));
    s->cards[s->count].body[0] = 0;
    s->count++;
    s->selected = -1;
    select_card(s, s->count - 1);
    s->focus = 1; s->modified = 1;
}
static void open(GWindow *w, const char *argument)
{
    gte_init(&S->edit, S->body, BODY_MAX);
    char path[256];
    if (argument) g_copy(path, argument, sizeof(path)); else g_path_join(path, sizeof(path), g_home(), "my.cards");
    S->selected = -1;
    load(w, path);
}
static const char *row(void *context, int index, int *icon)
{
    State *s = context;
    *icon = ICON_CARDFILE;
    return index == s->selected ? s->title.text : s->cards[index].title;
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    int lw = w->w / 3 < 150 ? 150 : w->w / 3, rows = (w->h - 46 - 4) / LIST_ROW;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    if (s->first > s->count - rows) s->first = s->count - rows;
    if (s->first < 0) s->first = 0;
    g_list_draw(c, 6, 6, lw, w->h - 44, s->count, s->first, s->selected, row, s, g_focused(w) && s->focus == 0);
    g_button(w, c, ID_ADD, 6, w->h - 32, lw / 2 - 2, 26, "Add");
    g_button(w, c, ID_DEL, 6 + lw / 2 + 2, w->h - 32, lw / 2 - 2, 26, "Delete");
    int x = lw + 14, cw = w->w - x - 8;
    if (s->selected < 0) { g_text(c, x, 12, "No card selected.", g_theme.text); return; }
    /* the card itself: white with a red title rule, like a paper index card */
    g_panel(c, x, 6, cw, w->h - 12, 0xfffef6, 0);
    g_text(c, x + 8, 12, "Title:", g_theme.shadow);
    g_edit_draw(c, x + 60, 8, cw - 68, &s->title, g_focused(w) && s->focus == 1);
    bgx_fill(c, x + 4, 36, cw - 8, 2, 0xd04040);
    gte_draw(&s->edit, c, x + 4, 42, cw - 8, w->h - 52, g_focused(w) && s->focus == 2);
    update_title(w);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int lw = w->w / 3 < 150 ? 150 : w->w / 3, x = lw + 14, cw = w->w - x - 8, rows = (w->h - 46 - 4) / LIST_ROW;
    if (e->type == EV_MOUSE_DOWN || e->type == EV_DOUBLE) {
        if (e->x >= 6 && e->x < 6 + lw - 16 && e->y >= 8 && e->y < w->h - 40) {
            int index = s->first + (e->y - 8) / LIST_ROW;
            s->focus = 0;
            if (index < s->count && index != s->selected) select_card(s, index);
        } else if (e->x >= x + 60 && e->y >= 8 && e->y < 30) s->focus = 1;
        else if (e->x >= x && e->y >= 42) { s->focus = 2; gte_mouse(&s->edit, e, x + 4, 42, cw - 8, w->h - 52); }
    } else if (e->type == EV_MOUSE_MOVE || e->type == EV_MOUSE_UP) gte_mouse(&s->edit, e, x + 4, 42, cw - 8, w->h - 52);
    else if (e->type == EV_WHEEL) {
        if (e->x < 6 + lw) { s->first += e->id * 3; } else gte_mouse(&s->edit, e, x + 4, 42, cw - 8, w->h - 52);
    } else if (e->type == EV_KEY) {
        if (e->code == BGX_KEY_TAB) { s->focus = (s->focus + 1) % 3; return; }
        if (e->key == 19) { save(w, s->path); return; }
        if (s->focus == 0) {
            if (e->code == BGX_KEY_UP && s->selected > 0) select_card(s, s->selected - 1);
            else if (e->code == BGX_KEY_DOWN && s->selected < s->count - 1) select_card(s, s->selected + 1);
            else if (e->key == '\n') s->focus = 2;
            else if (e->code == 0xd3) g_confirm(w, C_DELETE, "Cardfile", "Delete this card?");
            s->first = g_list_scroll(s->count, s->first, s->selected < 0 ? 0 : s->selected, rows);
        } else if (s->focus == 1) {
            int r = g_edit_key(&s->title, e);
            if (r == 1) s->modified = 1;
            if (r == 2) { sort(s); s->focus = 2; }
        } else if (gte_key(&s->edit, e, (w->h - 56) / 16)) s->modified = 1;
    } else if (e->type == EV_COMMAND) {
        if (e->id == ID_ADD) add(w);
        else if (e->id == ID_DEL && s->selected >= 0) g_confirm(w, C_DELETE, "Cardfile", "Delete this card?");
    } else if (e->type == EV_MENU) {
        char path[256];
        switch (e->id) {
        case M_NEW: add(w); break;
        case M_DELETE: if (s->selected >= 0) g_confirm(w, C_DELETE, "Cardfile", "Delete this card?"); break;
        case M_SAVE: save(w, s->path); break;
        case M_SAVEAS: g_prompt(w, P_SAVE, "Save As", "Card file (path):", s->path); break;
        case M_OPEN: g_path_join(path, sizeof(path), g_home(), ""); g_prompt(w, P_OPEN, "Open", "Card file (path):", path); break;
        case M_FIND: g_prompt(w, P_FIND, "Find", "Find text in cards:", ""); break;
        }
    } else if (e->type == EV_PROMPT && e->text[0]) {
        if (e->id == P_OPEN) load(w, e->text);
        else if (e->id == P_SAVE) save(w, e->text);
        else if (e->id == P_FIND) {
            store(s);
            int n = g_len(e->text);
            for (int k = 1; k <= s->count; k++) {
                int i = (s->selected + k) % s->count, found = 0;
                for (int pass = 0; pass < 2 && !found; pass++) {
                    const char *t = pass ? s->cards[i].body : s->cards[i].title;
                    for (int a = 0; t[a] && !found; a++) {
                        int b = 0; while (b < n && t[a + b] && (t[a + b] | 32) == (e->text[b] | 32)) b++;
                        found = b == n;
                    }
                }
                if (found) { select_card(s, i); break; }
            }
        }
    } else if (e->type == EV_CONFIRM && e->id == C_DELETE && s->selected >= 0) {
        for (int i = s->selected; i < s->count - 1; i++) s->cards[i] = s->cards[i + 1];
        s->count--;
        int next = s->selected >= s->count ? s->count - 1 : s->selected;
        s->selected = -1; s->modified = 1;
        select_card(s, next);
    }
}
static void close(GWindow *w) { if (S->modified) save(w, S->path); }
static const GMenu menus[] = {
    {"File", {{"Open...", M_OPEN}, {"Save   Ctrl+S", M_SAVE}, {"Save As...", M_SAVEAS}}},
    {"Card", {{"New", M_NEW}, {"Delete", M_DELETE}, {"-", 0}, {"Find...", M_FIND}}},
};
const GApp app_cardfile = {"Cardfile", "Index cards with notes", ICON_CARDFILE, 560, 320, 360, 200,
                           APP_RESIZABLE | APP_SINGLE, menus, 2, sizeof(State), open, event, 0, paint, close};
