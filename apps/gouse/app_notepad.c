/* Notepad: a plain-text editor with file menu and clipboard. */
#include "gouse.h"

#define TEXT_MAX 65536
enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXIT, M_CUT, M_COPY, M_PASTE, M_ALL, M_DATE,
       P_OPEN = 50, P_SAVE, C_NEW, C_OPEN, C_EXIT };
typedef struct {
    char text[TEXT_MAX];
    GTextEdit edit;
    char path[256];
} State;
#define S ((State *)w->state)

static const GMenu menus[] = {
    {"File", {{"New", M_NEW}, {"Open...", M_OPEN}, {"Save", M_SAVE}, {"Save As...", M_SAVEAS}, {"-", 0}, {"Exit", M_EXIT}}},
    {"Edit", {{"Cut      Ctrl+X", M_CUT}, {"Copy     Ctrl+C", M_COPY}, {"Paste    Ctrl+V", M_PASTE}, {"-", 0},
              {"Select All  Ctrl+A", M_ALL}, {"Time/Date", M_DATE}}},
};
static void update_title(GWindow *w)
{
    char title[300];
    const char *name = S->path;
    for (const char *p = S->path; *p; p++) if (*p == '/') name = p + 1;
    g_format(title, sizeof(title), "Notepad - %s%s", S->path[0] ? name : g_tr("(untitled)"), S->edit.modified ? " *" : "");
    if (!g_equal(title, w->title)) g_set_title(w, title);
}
static void load(GWindow *w, const char *path)
{
    static char data[TEXT_MAX];
    int n = g_read_file(path, data, TEXT_MAX - 1);
    if (n < 0) { g_error(w, "Notepad", "Cannot open this file."); return; }
    gte_set(&S->edit, data, n);
    g_copy(S->path, path, sizeof(S->path));
    update_title(w);
}
static void save(GWindow *w, const char *path)
{
    if (g_write_file(path, S->text, S->edit.length)) { g_error(w, "Notepad", "Cannot save the file there."); return; }
    g_copy(S->path, path, sizeof(S->path));
    S->edit.modified = 0;
    update_title(w);
}
static void ask_path(GWindow *w, int id, const char *title)
{
    char initial[256];
    if (S->path[0]) g_copy(initial, S->path, sizeof(initial));
    else g_path_join(initial, sizeof(initial), g_home(), "note.txt");
    g_prompt(w, id, title, "File name (path):", initial);
}
static void open(GWindow *w, const char *argument)
{
    gte_init(&S->edit, S->text, TEXT_MAX);
    if (argument) load(w, argument); else update_title(w);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    gte_draw(&S->edit, c, 0, 0, w->w, w->h, g_focused(w));
    update_title(w);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int rows = (w->h - 4) / 16;
    if (e->type == EV_KEY) {
        if (e->key == 19) { if (s->path[0]) save(w, s->path); else ask_path(w, P_SAVE, "Save As"); return; }   /* Ctrl+S */
        if (e->key == 15) { ask_path(w, P_OPEN, "Open"); return; }                                              /* Ctrl+O */
        gte_key(&s->edit, e, rows);
    } else if (e->type >= EV_MOUSE_DOWN && e->type <= EV_WHEEL) gte_mouse(&s->edit, e, 0, 0, w->w, w->h);
    else if (e->type == EV_DOUBLE) gte_mouse(&s->edit, e, 0, 0, w->w, w->h);
    else if (e->type == EV_MENU) {
        switch (e->id) {
        case M_NEW: if (s->edit.modified) g_confirm(w, C_NEW, "Notepad", "Discard the unsaved changes?"); else { gte_set(&s->edit, "", 0); s->path[0] = 0; } break;
        case M_OPEN: if (s->edit.modified) g_confirm(w, C_OPEN, "Notepad", "Discard the unsaved changes?"); else ask_path(w, P_OPEN, "Open"); break;
        case M_SAVE: if (s->path[0]) save(w, s->path); else ask_path(w, P_SAVE, "Save As"); break;
        case M_SAVEAS: ask_path(w, P_SAVE, "Save As"); break;
        case M_EXIT: if (s->edit.modified) g_confirm(w, C_EXIT, "Notepad", "Close without saving?"); else g_close(w); return;
        case M_CUT: gte_copy(&s->edit, 1); break;
        case M_COPY: gte_copy(&s->edit, 0); break;
        case M_PASTE: gte_insert(&s->edit, g_clipboard, g_clipboard_len); break;
        case M_ALL: s->edit.anchor = 0; s->edit.cursor = s->edit.length; break;
        case M_DATE: {
            GTime t; g_time(&t); char text[40];
            g_format(text, sizeof(text), "%02d:%02d %02d.%02d.%d", t.hour, t.minute, t.day, t.month, t.year);
            gte_insert(&s->edit, text, g_len(text));
            break;
        }
        }
    } else if (e->type == EV_PROMPT && e->text[0]) {
        if (e->id == P_OPEN) load(w, e->text);
        else if (e->id == P_SAVE) save(w, e->text);
    } else if (e->type == EV_CONFIRM) {
        if (e->id == C_NEW) { gte_set(&s->edit, "", 0); s->path[0] = 0; }
        else if (e->id == C_OPEN) ask_path(w, P_OPEN, "Open");
        else if (e->id == C_EXIT) { g_close(w); return; }
    }
    update_title(w);
}
const GApp app_notepad = {"Notepad", "Write and edit text files", ICON_NOTEPAD, 520, 320, 220, 120,
                          APP_RESIZABLE, menus, 2, sizeof(State), open, event, 0, paint, 0};
