/* Gouse Executive: the file manager and program launcher (MS-DOS Executive). */
#include "gouse.h"

extern const GApp app_notepad, app_paint, app_cardfile, app_media;
#define MAX_ITEMS 256
enum { ID_UP = 1, ID_HOME, ID_ROOT, ID_PROGRAMS, ID_OPEN, ID_NEWDIR, ID_RENAME, ID_DELETE, ID_COPY, ID_REFRESH,
       ID_SHORTCUT, ID_DESKTOP, ID_EXIT,
       P_NEWDIR = 100, P_RENAME, P_COPY, P_PATH, C_DELETE, M_NEWFILE };
typedef struct { char name[64]; unsigned int kind; } Item;
typedef struct {
    char path[256];
    Item items[MAX_ITEMS];
    int count, first, selected;
    char status[96];
} State;
#define S ((State *)w->state)

static int before(const Item *a, const Item *b)
{
    if ((a->kind == 4) != (b->kind == 4)) return a->kind == 4;
    const char *x = a->name, *y = b->name;
    while (*x && (*x | 32) == (*y | 32)) { x++; y++; }
    return (*x | 32) < (*y | 32);
}
static void refresh(GWindow *w, const char *keep)
{
    State *s = S;
    unsigned int cursor = 0; BarnixDirEntry entry; int result;
    s->count = 0;
    while ((result = barnix->directory(s->path, &cursor, &entry)) > 0 && s->count < MAX_ITEMS) {
        if (g_equal(entry.name, ".") || g_equal(entry.name, "..")) continue;
        g_copy(s->items[s->count].name, entry.name, sizeof(s->items[0].name));
        s->items[s->count++].kind = entry.kind;
    }
    for (int i = 1; i < s->count; i++) {
        Item v = s->items[i]; int j = i;
        while (j > 0 && before(&v, &s->items[j - 1])) { s->items[j] = s->items[j - 1]; j--; }
        s->items[j] = v;
    }
    s->selected = s->count ? 0 : -1; s->first = 0;
    for (int i = 0; keep && i < s->count; i++) if (g_equal(s->items[i].name, keep)) s->selected = i;
    if (result < 0) g_copy(s->status, result == -13 ? "Access denied: only root can list this folder" : "Cannot read this folder", sizeof(s->status));
    else g_format(s->status, sizeof(s->status), "%d items", s->count);
    char title[300]; g_format(title, sizeof(title), "Executive - %s", s->path);
    g_set_title(w, title);
}
static void go(GWindow *w, const char *path)
{
    State *s = S;
    char next[256];
    if (g_equal(path, "..")) {
        g_copy(next, s->path, sizeof(next));
        int n = g_len(next);
        while (n > 1 && next[n - 1] != '/') n--;
        next[n > 1 ? n - 1 : 1] = 0;
    } else g_path_join(next, sizeof(next), s->path, path);
    unsigned int cursor = 0; BarnixDirEntry entry;
    int result = barnix->directory(next, &cursor, &entry);
    if (result < 0) {
        g_error(w, "Executive", result == -13 ? "Access denied. Only root can list this folder;\nits programs can still be run by name." : "This folder cannot be opened.");
        return;
    }
    char from[64] = "";
    if (g_equal(path, "..")) { int n = g_len(s->path); while (n > 0 && s->path[n - 1] != '/') n--; g_copy(from, s->path + n, sizeof(from)); }
    g_copy(s->path, next, sizeof(s->path));
    refresh(w, from);
}
static void full_path(GWindow *w, const char *name, char *out, int capacity) { g_path_join(out, capacity, S->path, name); }
static int is_text(const char *name)
{
    static const char *const kinds[] = {".txt", ".md", ".cfg", ".c", ".h", ".py", ".sh", ".asm", ".log", ".ini", ".json", ".csv", ".bcc"};
    for (unsigned int i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) if (g_ends_ci(name, kinds[i])) return 1;
    int dot = 0; for (const char *p = name; *p; p++) if (*p == '.') dot = 1;
    return !dot || name[0] == '.';
}
static void open_item(GWindow *w)
{
    State *s = S;
    if (s->selected < 0) return;
    Item *item = &s->items[s->selected];
    char path[320];
    full_path(w, item->name, path, sizeof(path));
    if (item->kind == 4) { go(w, item->name); return; }
    if (g_ends_ci(item->name, ".bmp")) { g_open(&app_paint, path); return; }
    if (g_ends_ci(item->name, ".cards")) { g_open(&app_cardfile, path); return; }
    if (g_ends_ci(item->name, ".desktop")) { if (g_open_shortcut(path)) g_error(w, "Executive", "This shortcut cannot be opened."); return; }
    if (g_ends_ci(item->name, ".html") || g_ends_ci(item->name, ".htm") || g_ends_ci(item->name, ".xhtml")) { char url[340]; g_format(url, sizeof(url), "file://%s", path); g_browse(url); return; }
    if (g_ends_ci(item->name, ".wav")) {
        GWindow *player = g_find(&app_media);
        if (player) { g_focus(player); media_open_file(player, path); }
        else g_open(&app_media, path);
        return;
    }
    int binary = 0;
    char head[4];
    if (barnix->read(path, 0, head, 4) == 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') binary = 1;
    if (binary || g_ends_ci(item->name, ".elf")) g_run_program(w, path);
    else if (is_text(item->name) || barnix->size(path) < 65536) g_open(&app_notepad, path);
}
static const char *row_text(void *context, int index, int *icon)
{
    State *s = context;
    Item *item = &s->items[index];
    *icon = item->kind == 4 ? ICON_EXECUTIVE : g_ends_ci(item->name, ".elf") ? ICON_PROGRAM :
            g_ends_ci(item->name, ".wav") ? ICON_MEDIA : g_ends_ci(item->name, ".desktop") ? ICON_LINK :
            g_ends_ci(item->name, ".html") || g_ends_ci(item->name, ".htm") ? ICON_BROWSER : ICON_DOCUMENT;
    if ((g_equal(s->path, "/usr/bin") || g_equal(s->path, "/bin")) && item->kind != 4) *icon = ICON_PROGRAM;
    return item->name;
}
static void list_rect(GWindow *w, int *x, int *y, int *width, int *height)
{ *x = 6; *y = 62; *width = w->w - 12; *height = w->h - 62 - 26; }

static void open(GWindow *w, const char *argument)
{
    if (argument) g_copy(S->path, argument, sizeof(S->path));
    else if (barnix->getcwd(S->path, sizeof(S->path)) < 0) g_copy(S->path, "/", sizeof(S->path));
    refresh(w, 0);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    static const char *const english[] = {"Up", "Home", "Root", "Programs", "Open", "New folder", "Rename", "Delete", "Copy", "Refresh"};
    /* Short words, so the Russian toolbar fits as the English one does. */
    static const char *const russian[] = {"Вверх", "Дом", "Корень", "Прогр.", "Открыть", "Папка", "Переим.", "Удалить", "Копия", "Обнов."};
    const char *const *labels = g_russian ? russian : english;
    int x = 6;
    for (int i = 0; i < 10; i++) {
        int bw = bgx_text_width(labels[i], 1) + 14;
        if (x + bw > w->w - 6) break;
        g_button(w, c, ID_UP + i, x, 6, bw, 24, labels[i]);
        x += bw + 3;
    }
    g_panel(c, 6, 34, w->w - 12, 24, g_theme.window, 1);
    g_icon(c, 10, 38, ICON_FOLDER_OPEN, 16);
    g_text_fit(c, 30, 38, w->w - 44, s->path, g_theme.text);
    int lx, ly, lw, lh; list_rect(w, &lx, &ly, &lw, &lh);
    if (s->first > s->count - (lh - 4) / LIST_ROW) s->first = s->count - (lh - 4) / LIST_ROW;
    if (s->first < 0) s->first = 0;
    g_list_draw(c, lx, ly, lw, lh, s->count, s->first, s->selected, row_text, s, g_focused(w));
    g_text_fit(c, 8, w->h - 21, w->w - 16, s->status, g_theme.text);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int lx, ly, lw, lh; list_rect(w, &lx, &ly, &lw, &lh);
    int rows = (lh - 4) / LIST_ROW;
    char path[320], text[320];
    const char *name = s->selected >= 0 ? s->items[s->selected].name : "";
    if (e->type == EV_MOUSE_DOWN && e->x >= lx && e->y >= ly + 2 && e->x < lx + lw - 16 && e->y < ly + lh - 2) {
        int index = s->first + (e->y - ly - 2) / LIST_ROW;
        if (index < s->count) s->selected = index;
    } else if (e->type == EV_DOUBLE && e->x >= lx && e->y >= ly && e->x < lx + lw - 16 && e->y < ly + lh) open_item(w);
    else if (e->type == EV_WHEEL) { s->first += e->id * 3; if (s->first > s->count - rows) s->first = s->count - rows; if (s->first < 0) s->first = 0; }
    else if (e->type == EV_KEY) {
        if (e->code == BGX_KEY_UP && s->selected > 0) s->selected--;
        else if (e->code == BGX_KEY_DOWN && s->selected < s->count - 1) s->selected++;
        else if (e->code == 0xc9) { s->selected -= rows; if (s->selected < 0) s->selected = s->count ? 0 : -1; }
        else if (e->code == 0xd1) { s->selected += rows; if (s->selected >= s->count) s->selected = s->count - 1; }
        else if (e->code == 0xc7) s->selected = s->count ? 0 : -1;
        else if (e->code == 0xcf) s->selected = s->count - 1;
        else if (e->key == '\n') open_item(w);
        else if (e->key == '\b') go(w, "..");
        else if (e->code == 0xd3 && s->selected >= 0) { g_format(text, sizeof(text), "Delete %s?", name); g_confirm(w, C_DELETE, "Delete", text); }
        else if (e->code == 0x3c && s->selected >= 0) g_prompt(w, P_RENAME, "Rename", "New name:", name);   /* F2 */
        else if (e->code == 0x3f) refresh(w, name);   /* F5 */
        else if ((e->key == 3 || e->key == 24) && s->selected >= 0) {   /* Ctrl+C / Ctrl+X: the file's path */
            g_path_join(path, sizeof(path), s->path, name);
            g_clipboard_set(path, g_len(path));
            g_format(s->status, sizeof(s->status), g_tr("Copied: %s (Ctrl+V puts a copy into another folder)"), path);
        } else if (e->key == 22) {   /* Ctrl+V: a copy of the file whose path is in the clipboard */
            char from[256] = ""; int n = 0;
            for (int i = 0; i < g_clipboard_len && n < 255 && g_clipboard[i] != '\n'; i++) from[n++] = g_clipboard[i];
            from[n] = 0;
            const char *base = from;
            for (const char *p = from; *p; p++) if (*p == '/') base = p + 1;
            if (from[0] != '/' || !base[0] || barnix->size(from) < 0) { g_copy(s->status, g_tr("The clipboard holds no file path."), sizeof(s->status)); }
            else {
                g_path_join(path, sizeof(path), s->path, base);
                if (g_equal(path, from)) {   /* the same folder: "copy of NAME" */
                    char other[96]; g_format(other, sizeof(other), "copy-%s", base);
                    g_path_join(path, sizeof(path), s->path, other);
                }
                if (barnix->cp(from, path)) g_error(w, "Executive", "The file cannot be copied here.");
                else { const char *made = path; for (const char *p = path; *p; p++) if (*p == '/') made = p + 1; refresh(w, made); }
            }
        } else if (e->key >= 'a' && e->key <= 'z') {   /* jump to a name */
            for (int k = 1; k <= s->count; k++) {
                int i = (s->selected + k) % s->count;
                if ((s->items[i].name[0] | 32) == (int)e->key) { s->selected = i; break; }
            }
        }
    } else if (e->type == EV_COMMAND || e->type == EV_MENU) {
        switch (e->id) {
        case ID_SHORTCUT:
            if (s->selected >= 0) {
                full_path(w, name, path, sizeof(path));
                if (g_shortcut_for(path, 0)) g_error(w, "Executive", "Cannot make the shortcut (is ~/Desktop writable?).");
                else g_format(s->status, sizeof(s->status), g_tr("A shortcut to %s is on the desktop"), name);
            }
            return;
        case ID_DESKTOP: g_path_join(s->path, sizeof(s->path), g_home(), "Desktop"); refresh(w, 0); break;
        case ID_EXIT: g_close(w); return;
        case ID_UP: go(w, ".."); break;
        case ID_HOME: g_copy(s->path, g_home(), sizeof(s->path)); refresh(w, 0); break;
        case ID_ROOT: g_copy(s->path, "/", sizeof(s->path)); refresh(w, 0); break;
        case ID_PROGRAMS: { unsigned int c = 0; BarnixDirEntry e2; if (barnix->directory("/usr/bin", &c, &e2) == -13) g_prompt(w, P_PATH, "Programs", "Only root can list /usr/bin. Program to run:", "/usr/bin/"); else { g_copy(s->path, "/usr/bin", sizeof(s->path)); refresh(w, 0); } break; }
        case ID_OPEN: open_item(w); break;
        case ID_NEWDIR: g_prompt(w, P_NEWDIR, "New folder", "Folder name:", "New folder"); break;
        case ID_RENAME: if (s->selected >= 0) g_prompt(w, P_RENAME, "Rename", "New name:", name); break;
        case ID_COPY: if (s->selected >= 0 && s->items[s->selected].kind != 4) { g_format(text, sizeof(text), "%s.copy", name); g_prompt(w, P_COPY, "Copy", "Copy to (name or path):", text); } break;
        case ID_DELETE: if (s->selected >= 0) { g_format(text, sizeof(text), "Delete %s?", name); g_confirm(w, C_DELETE, "Delete", text); } break;
        case ID_REFRESH: refresh(w, name); break;
        }
    } else if (e->type == EV_PROMPT && e->text[0]) {
        char target[320];
        full_path(w, e->text, target, sizeof(target));
        full_path(w, name, path, sizeof(path));
        int failed = 0;
        if (e->id == P_NEWDIR) failed = barnix->mkdir(target);
        else if (e->id == P_RENAME) failed = barnix->mv(path, target);
        else if (e->id == P_COPY) failed = barnix->cp(path, target);
        else if (e->id == P_PATH) { g_run_program(w, e->text); return; }
        if (failed) g_error(w, "Executive", "The operation failed (name, permissions or space).");
        int n = g_len(e->text); while (n > 0 && e->text[n - 1] != '/') n--;
        refresh(w, e->text + n);
    } else if (e->type == EV_CONFIRM && e->id == C_DELETE && s->selected >= 0) {
        full_path(w, name, path, sizeof(path));
        int failed = s->items[s->selected].kind == 4 ? barnix->rmdir(path) : barnix->rm(path);
        if (failed) g_error(w, "Executive", "Cannot delete it (folder not empty or access denied).");
        refresh(w, 0);
    }
    if (s->selected >= 0 && s->selected < s->count && e->type != EV_COMMAND && e->type != EV_PROMPT && e->type != EV_CONFIRM)
        g_format(s->status, sizeof(s->status), "%s%s   (%d items)", name, s->items[s->selected].kind == 4 ? g_tr("  [folder]") : "", s->count);
    if (e->type == EV_KEY) s->first = g_list_scroll(s->count, s->first, s->selected < 0 ? 0 : s->selected, rows);
}
static const GMenu menus[] = {
    {"File", {{"Open          Enter", ID_OPEN}, {"Shortcut on desktop", ID_SHORTCUT}, {"-", 0}, {"New folder", ID_NEWDIR},
              {"Rename           F2", ID_RENAME}, {"Copy", ID_COPY}, {"Delete          Del", ID_DELETE}, {"-", 0}, {"Exit", ID_EXIT}}},
    {"Go", {{"Up         Backspace", ID_UP}, {"Home", ID_HOME}, {"Desktop", ID_DESKTOP}, {"Root", ID_ROOT}, {"Programs", ID_PROGRAMS},
            {"-", 0}, {"Refresh          F5", ID_REFRESH}}},
};
const GApp app_executive = {"Executive", "Files and programs", ICON_EXECUTIVE, 560, 340, 360, 200,
                            APP_RESIZABLE, menus, 2, sizeof(State), open, event, 0, paint, 0};
