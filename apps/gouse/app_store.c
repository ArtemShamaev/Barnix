/* BarnStore: the package manager brm with windows and buttons. It reads
 * brm's database (/etc/brm/sync, local, base.d) and runs brm itself in its
 * window (ABI 14 run_area) for every change, so downloads show their
 * progress there. A user who is not root types root's password once. */
#include "gouse.h"

#define MAX_PACKAGES 384
#define LIST_TOP 40
#define SIDE_W 150
#define ROW_H 40
#define DETAIL_H 128
enum { B_REFRESH = 1, B_UPGRADE, B_INSTALL, B_REMOVE, B_RUN, B_OK, B_CANCEL, B_CATEGORY = 100, B_ROW = 200 };
enum { M_REFRESH = 20, M_UPGRADE, M_CLOSE, M_ABOUT, M_REPO };
enum { BASE = 1, ESSENTIAL = 2 };

typedef struct {
    char name[24], version[32], file[96], description[100], description_ru[160], category[16];
    unsigned int size, installed_kib, flags;
    char installed[32];   /* the installed version, "" when not installed */
} Package;
typedef struct {
    Package packages[MAX_PACKAGES];
    int count, shown[MAX_PACKAGES], shown_count;
    int category, selected, top;
    GEdit search; int search_focus;
    char status[256];
    int synced;
    /* root's password, asked before a change */
    int asking; GEdit password; char pending[200];
    char remembered[64]; unsigned int remembered_at;
    char want[24];   /* select this package once the list is in (the IDE asks for gcc) */
} State;
#define S ((State *)w->state)

static const char *const categories[] = {"All", "Installed", "Updates", "Games", "Desktop", "Development",
                                         "Network", "Graphics", "Multimedia", "System", "Apps"};
static const char *const category_ids[] = {"", "", "", "games", "desktop", "devel", "network", "graphics", "multimedia", "system", "apps"};
#define CATEGORY_COUNT 11

/* ---------------------------------------------------------------- brm's database */
static int compare_versions(const char *a, const char *b)
{
    while (*a || *b) {
        unsigned int x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (unsigned int)(*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (unsigned int)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (*a && (*a < '0' || *a > '9')) a++;
        if (*b && (*b < '0' || *b > '9')) b++;
    }
    return 0;
}
static int split(char *line, char **part, int max)
{
    int n = 0;
    part[n++] = line;
    for (char *c = line; *c && n < max; c++) if (*c == '|') { *c = 0; part[n++] = c + 1; }
    return n;
}
static Package *find(State *s, const char *name)
{
    for (int i = 0; i < s->count; i++) if (g_equal(s->packages[i].name, name)) return &s->packages[i];
    return 0;
}
static void each_line(char *text, void (*use)(State *, char *), State *s)
{
    for (char *p = text; *p;) {
        char *e = p; while (*e && *e != '\n') e++;
        char saved = *e; *e = 0;
        if (e > p && e[-1] == '\r') e[-1] = 0;
        if (*p && *p != '#') use(s, p);
        *e = saved; p = *e ? e + 1 : e;
    }
}
static void repo_line(State *s, char *line)
{
    char *part[10];
    int n = split(line, part, 10);
    if (n < 7 || s->count >= MAX_PACKAGES) return;
    Package *p = &s->packages[s->count++];
    for (unsigned int i = 0; i < sizeof(*p); i++) ((char *)p)[i] = 0;
    g_copy(p->name, part[0], sizeof(p->name)); g_copy(p->version, part[1], sizeof(p->version));
    g_copy(p->file, part[2], sizeof(p->file)); p->size = (unsigned int)g_atoi(part[3]);
    g_copy(p->description, part[5], sizeof(p->description)); g_copy(p->description_ru, part[6], sizeof(p->description_ru));
    g_copy(p->category, n > 7 && part[7][0] ? part[7] : "apps", sizeof(p->category));
    if (n > 8) { for (char *f = part[8]; *f; f++) { if (*f == 'b' && f[1] == 'a') p->flags |= BASE; if (*f == 'e' && f[1] == 's') p->flags |= ESSENTIAL; } }
    p->installed_kib = n > 9 ? (unsigned int)g_atoi(part[9]) : (p->size + 1023) / 1024;
}
static void local_line(State *s, char *line)
{
    char *part[6];
    int n = split(line, part, 6);
    if (n < 2) return;
    Package *p = find(s, part[0]);
    if (!p) {   /* installed but not in the repository */
        if (s->count >= MAX_PACKAGES) return;
        p = &s->packages[s->count++];
        for (unsigned int i = 0; i < sizeof(*p); i++) ((char *)p)[i] = 0;
        g_copy(p->name, part[0], sizeof(p->name)); g_copy(p->version, part[1], sizeof(p->version));
        if (n > 2) g_copy(p->description, part[2], sizeof(p->description));
        if (n > 3) g_copy(p->description_ru, part[3], sizeof(p->description_ru));
        g_copy(p->category, n > 4 && part[4][0] ? part[4] : "apps", sizeof(p->category));
    }
    if (!p->installed[0]) g_copy(p->installed, part[1], sizeof(p->installed));
}
static void reload(GWindow *w)
{
    State *s = S;
    enum { TEXT = 196608 };
    static char *text;
    if (!text) text = g_alloc(TEXT);
    s->count = 0;
    if (!text) return;
    int n = g_read_file("/etc/brm/sync", text, TEXT - 1);
    s->synced = n >= 0;
    if (s->synced) { text[n] = 0; each_line(text, repo_line, s); }
    n = g_read_file("/etc/brm/local", text, TEXT - 1);
    if (n >= 0) { text[n] = 0; each_line(text, local_line, s); }
    unsigned int position = 0; BarnixDirEntry entry;
    while (barnix->directory("/etc/brm/base.d", &position, &entry) > 0) {
        if (entry.kind != 8) continue;
        char path[300]; g_path_join(path, sizeof(path), "/etc/brm/base.d", entry.name);
        n = g_read_file(path, text, TEXT - 1);
        if (n >= 0) { text[n] = 0; each_line(text, local_line, s); }
    }
}
static int upgradable(const Package *p) { return p->installed[0] && p->file[0] && compare_versions(p->installed, p->version) < 0; }
static const char *describe(const Package *p) { return g_russian && p->description_ru[0] ? p->description_ru : p->description; }
static int matches(const char *text, const char *needle)
{
    if (!*needle) return 1;
    for (; *text; text++) {
        int i = 0;
        while (needle[i] && text[i]) {
            char a = text[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            i++;
        }
        if (!needle[i]) return 1;
    }
    return 0;
}
static void filter(GWindow *w)
{
    State *s = S;
    s->shown_count = 0;
    /* applications and games first, then the rest; alphabetical inside */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->count; i++) {
            Package *p = &s->packages[i];
            int system = g_equal(p->category, "system");
            if ((pass == 0) == system) continue;
            if (s->category == 1 && !p->installed[0]) continue;
            if (s->category == 2 && !upgradable(p)) continue;
            if (s->category >= 3 && !g_equal(p->category, category_ids[s->category])) continue;
            if (!matches(p->name, s->search.text) && !matches(p->description, s->search.text) && !matches(p->description_ru, s->search.text)) continue;
            int at = s->shown_count++;
            while (at > 0) {
                Package *q = &s->packages[s->shown[at - 1]];
                if (g_equal(q->category, "system") != system) break;
                int k = 0; while (q->name[k] && q->name[k] == p->name[k]) k++;
                if ((unsigned char)q->name[k] <= (unsigned char)p->name[k]) break;
                s->shown[at] = s->shown[at - 1]; at--;
            }
            s->shown[at] = i;
        }
    if (s->selected >= s->shown_count) s->selected = s->shown_count - 1;
    if (s->selected < 0 && s->shown_count) s->selected = 0;
    if (s->top > s->selected) s->top = s->selected < 0 ? 0 : s->selected;
}
static Package *current(GWindow *w)
{
    State *s = S;
    return s->selected >= 0 && s->selected < s->shown_count ? &s->packages[s->shown[s->selected]] : 0;
}

/* ---------------------------------------------------------------- running brm */
static void run_brm(GWindow *w, const char *arguments)
{
    State *s = S;
    static void *swap; static char *output;
    if (!swap) swap = g_alloc(APP_STACK_TOP - APP_LOAD_MIN);
    if (!output) output = g_alloc(65536);
    if (!swap || !output || barnix->version < 14) { g_error(w, "BarnStore", "This Barnix is too old for BarnStore: use brm in the shell."); return; }
    char line[200]; g_copy(line, arguments, sizeof(line));
    const char *argv[12] = {"brm"}; int argc = 1;
    for (char *p = line; *p && argc < 11;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc++] = "--noconfirm";
    g_copy(s->status, g_tr("Working... (the progress is shown in this window)"), sizeof(s->status));
    w->busy = 1;
    g_invalidate(w); g_flush();
    barnix->run_area(w->x, w->y, w->w, w->h);
    if (!g_equal(barnix->current_user(), "root")) barnix->sudo(s->remembered);
    int status = 0;
    int n = barnix->run("/usr/bin/brm", argc, argv, output, 65536, swap, &status);
    bgx_keys_release();
    w->busy = 0;
    reload(w); filter(w);
    if (n < 0) { g_error(w, "BarnStore", "brm cannot be run (is it installed?)."); return; }
    /* the last meaningful line is the answer */
    int end = n;
    while (end > 0 && (output[end - 1] == '\n' || output[end - 1] == ' ' || output[end - 1] == '\r')) end--;
    int start = end;
    while (start > 0 && output[start - 1] != '\n' && output[start - 1] != '\f') start--;
    output[end] = 0;
    if (status) g_format(s->status, sizeof(s->status), "%s %s", g_tr("Failed:"), output + start);
    else g_format(s->status, sizeof(s->status), "%s %s", g_tr("Done:"), output + start);
    if (status) g_sound(G_SOUND_ERROR); else g_sound(G_SOUND_DING);
    g_invalidate(w);
}
/* Changes need root: the password is asked once and remembered a while. */
static void change(GWindow *w, const char *arguments)
{
    State *s = S;
    if (g_equal(barnix->current_user(), "root") ||
        (s->remembered_at && g_now() - s->remembered_at < 300000 && !barnix->sudo(s->remembered))) {
        run_brm(w, arguments);
        return;
    }
    g_copy(s->pending, arguments, sizeof(s->pending));
    g_edit_set(&s->password, "");
    s->asking = 1;
}
static void password_done(GWindow *w, int accepted)
{
    State *s = S;
    s->asking = 0;
    if (!accepted) { g_edit_set(&s->password, ""); return; }
    if (barnix->sudo(s->password.text)) {
        g_edit_set(&s->password, "");
        g_error(w, "BarnStore", "Wrong root password.");
        return;
    }
    g_copy(s->remembered, s->password.text, sizeof(s->remembered));
    s->remembered_at = g_now(); if (!s->remembered_at) s->remembered_at = 1;
    g_edit_set(&s->password, "");
    run_brm(w, s->pending);
}
static void act(GWindow *w, int id)
{
    State *s = S;
    Package *p = current(w);
    char arguments[200];
    switch (id) {
    case B_REFRESH: case M_REFRESH: change(w, "-Sy"); break;
    case B_UPGRADE: case M_UPGRADE: change(w, s->synced ? "-Syu" : "-Sy"); break;
    case B_INSTALL:
        if (!p || !p->file[0]) break;
        g_format(arguments, sizeof(arguments), "-S %s", p->name);
        change(w, arguments);
        break;
    case B_REMOVE:
        if (!p || !p->installed[0]) break;
        if (p->flags & ESSENTIAL) { g_error(w, "BarnStore", "This package is a part of Barnix and cannot be removed."); break; }
        g_format(arguments, sizeof(arguments), "-R %s", p->name);
        change(w, arguments);
        break;
    case B_RUN:
        if (!p) break;
        if (g_equal(p->name, "gcc") || g_equal(p->name, "python")) { g_open(g_app_named("Barnix Code"), 0); break; }
        if (g_equal(p->name, "gouse")) break;
        char path[64]; g_path_join(path, sizeof(path), "/usr/bin", p->name);
        g_run_program(w, path);
        break;
    case M_REPO: g_open(g_app_named("Gouse Prompt"), "brm repo"); break;
    case M_ABOUT: g_message(w, "BarnStore", "BarnStore 1.0: programs for Barnix.\nIt works through brm, the Barnix\nRepository Manager, like pacman."); break;
    case M_CLOSE: g_close(w); break;
    }
}

/* ---------------------------------------------------------------- window */
static void open(GWindow *w, const char *argument)
{
    State *s = S;
    s->selected = -1;
    if (argument && argument[0]) { g_copy(s->want, argument, sizeof(s->want)); g_edit_set(&s->search, argument); }
    reload(w); filter(w);
    if (!s->synced) g_copy(s->status, g_tr("Press Refresh to download the list of programs."), sizeof(s->status));
    else g_format(s->status, sizeof(s->status), g_tr("%d packages. Double-click one to install it."), s->count);
}
static void tick(GWindow *w, int dt)
{
    (void)dt;
    State *s = S;
    if (!s->want[0]) return;
    if (!s->synced) { s->want[0] = 0; return; }
    for (int i = 0; i < s->shown_count; i++)
        if (g_equal(s->packages[s->shown[i]].name, s->want)) { s->selected = i; g_invalidate(w); break; }
    s->want[0] = 0;
}
static void list_geometry(GWindow *w, int *x, int *y, int *lw, int *lh)
{ *x = SIDE_W + 8; *y = LIST_TOP; *lw = w->w - SIDE_W - 14; *lh = w->h - LIST_TOP - DETAIL_H - 32; }
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    /* top: search and the global buttons */
    g_text(c, 8, 12, "Search:", g_theme.text);
    int sx = 8 + bgx_text_width("Search:", 1) + 8;
    int bw = g_russian ? 128 : 100;
    g_edit_draw(c, sx, 8, w->w - sx - 2 * bw - 20, &s->search, g_focused(w) && !s->asking);
    g_button(w, c, B_REFRESH, w->w - 2 * bw - 10, 7, bw, 24, "Refresh");
    g_button(w, c, B_UPGRADE, w->w - bw - 6, 7, bw, 24, "Update all");
    /* categories */
    g_panel(c, 6, LIST_TOP, SIDE_W - 2, w->h - LIST_TOP - 28, g_theme.window, 1);
    for (int i = 0; i < CATEGORY_COUNT; i++) {
        int y = LIST_TOP + 4 + i * 22;
        if (i == s->category) bgx_fill(c, 9, y - 2, SIDE_W - 8, 20, g_theme.highlight);
        int updates = 0;
        if (i == 2) for (int k = 0; k < s->count; k++) updates += upgradable(&s->packages[k]);
        char label[48]; g_format(label, sizeof(label), i == 2 && updates ? "%s (%d)" : "%s", g_tr(categories[i]), updates);
        g_text_raw(c, 14, y, label, i == s->category ? g_theme.highlight_text : g_theme.text);
        g_zone(w, B_CATEGORY + i, 8, y - 2, SIDE_W - 6, 20);
    }
    /* the list */
    int lx, ly, lw, lh;
    list_geometry(w, &lx, &ly, &lw, &lh);
    g_panel(c, lx, ly, lw, lh, g_theme.window, 1);
    int rows = (lh - 4) / ROW_H;
    if (s->selected >= 0 && s->selected < s->top) s->top = s->selected;
    if (s->selected >= s->top + rows) s->top = s->selected - rows + 1;
    if (!s->shown_count)
        g_text_center(c, lx, ly + lh / 2 - 8, lw, s->synced ? "Nothing found." : "The list of programs is not downloaded yet: press Refresh.", g_theme.shadow);
    for (int r = 0; r < rows && s->top + r < s->shown_count; r++) {
        Package *p = &s->packages[s->shown[s->top + r]];
        int y = ly + 2 + r * ROW_H, selected = s->top + r == s->selected;
        if (selected) bgx_fill(c, lx + 2, y, lw - 18, ROW_H, g_theme.highlight);
        unsigned int text = selected ? g_theme.highlight_text : g_theme.text, dim = selected ? g_theme.highlight_text : g_theme.shadow;
        int icon = g_equal(p->category, "games") ? ICON_REVERSI : g_equal(p->category, "devel") ? ICON_CODE :
                   g_equal(p->category, "network") ? ICON_BROWSER : g_equal(p->category, "graphics") ? ICON_PAINT :
                   g_equal(p->category, "multimedia") ? ICON_MEDIA : g_equal(p->category, "desktop") ? ICON_EXECUTIVE :
                   g_equal(p->category, "system") ? ICON_CONTROL : ICON_PROGRAM;
        g_icon(c, lx + 6, y + 4, icon, 32);
        g_text_raw(c, lx + 46, y + 3, p->name, text);
        int nx = lx + 46 + g_len(p->name) * 8 + 10;
        g_text_raw(c, nx, y + 3, p->version, dim);
        const char *badge = upgradable(p) ? "update" : p->installed[0] ? "installed" : "";
        if (badge[0]) {
            int bwid = bgx_text_width(badge, 1) + 10, bx = lx + lw - 24 - bwid;
            bgx_fill(c, bx, y + 3, bwid, 16, upgradable(p) ? 0xd98c1f : 0x3f9f4f);
            bgx_text(c, bx + 5, y + 3, badge, 0xffffff, 1);
        }
        g_text_fit_raw(c, lx + 46, y + 20, lw - 76, describe(p), dim);
        g_zone(w, B_ROW + r, lx + 2, y, lw - 18, ROW_H);
    }
    if (s->shown_count > rows) g_scrollbar(c, lx + lw - 16, ly + 2, lh - 4, s->shown_count, s->top, rows);
    /* details of the selected package */
    int dy = ly + lh + 4;
    g_panel(c, lx, dy, lw, DETAIL_H - 2, g_theme.window, 1);
    Package *p = current(w);
    if (p) {
        char line[200];
        g_format(line, sizeof(line), "%s %s", p->name, p->version);
        bgx_text(c, lx + 10, dy + 6, line, g_theme.text, 2);
        g_text_fit_raw(c, lx + 10, dy + 40, lw - 20, describe(p), g_theme.text);
        char size[32], installed[32];
        if (p->size >= 10 << 20) g_format(size, sizeof(size), "%u MiB", p->size >> 20); else g_format(size, sizeof(size), "%u KiB", (p->size + 1023) / 1024);
        if (p->installed_kib >= 10240) g_format(installed, sizeof(installed), "%u MiB", p->installed_kib / 1024); else g_format(installed, sizeof(installed), "%u KiB", p->installed_kib);
        if (p->file[0]) g_format(line, sizeof(line), g_tr("Category: %s   Download: %s   On disk: %s"), p->category, size, installed);
        else g_format(line, sizeof(line), g_tr("Category: %s   (not in the repository)"), p->category);
        g_text_fit(c, lx + 10, dy + 60, lw - 20, line, g_theme.shadow);
        if (p->installed[0]) g_format(line, sizeof(line), g_tr("Installed: %s%s"), p->installed, p->flags & BASE ? g_tr(" (part of Barnix)") : "");
        else g_copy(line, g_tr("Not installed"), sizeof(line));
        g_text_fit(c, lx + 10, dy + 78, lw - 20, line, g_theme.shadow);
        int x = lx + 10, by = dy + DETAIL_H - 34, b = g_russian ? 120 : 96;
        if (p->file[0]) { g_button(w, c, B_INSTALL, x, by, b, 26, upgradable(p) ? "Update" : p->installed[0] ? "Reinstall" : "Install"); x += b + 6; }
        if (p->installed[0] && !(p->flags & ESSENTIAL)) { g_button(w, c, B_REMOVE, x, by, b, 26, "Remove"); x += b + 6; }
        if (p->installed[0] && !g_equal(p->name, "gouse")) g_button(w, c, B_RUN, x, by, b, 26, g_equal(p->name, "gcc") || g_equal(p->name, "python") ? "Open Code" : "Run");
    } else g_text(c, lx + 10, dy + 10, "Choose a package.", g_theme.shadow);
    g_text_fit_raw(c, 8, w->h - 22, w->w - 16, s->status, g_theme.text);
    /* the root password bar */
    if (s->asking) {
        int bh = 84, by = (w->h - bh) / 2, bx = 40, bwid = w->w - 80;
        g_panel(c, bx, by, bwid, bh, g_theme.face, 0);
        bgx_frame(c, bx, by, bwid, bh, 2, g_theme.title_active);
        g_text(c, bx + 12, by + 10, "Changes need root's password:", g_theme.text);
        GEdit masked; int n = 0;
        for (const char *q = s->password.text; *q && n < 200; q++) if (((unsigned char)*q & 0xc0) != 0x80) masked.text[n++] = '*';
        masked.text[n] = 0; masked.cursor = n;
        g_edit_draw(c, bx + 12, by + 32, bwid - 2 * 12 - 2 * 86, &masked, 1);
        g_button(w, c, B_OK, bx + bwid - 176, by + 31, 80, 24, "OK");
        g_button(w, c, B_CANCEL, bx + bwid - 92, by + 31, 80, 24, "Cancel");
    }
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int lx, ly, lw, lh;
    list_geometry(w, &lx, &ly, &lw, &lh);
    int rows = (lh - 4) / ROW_H;
    if (s->asking) {
        if (e->type == EV_KEY) {
            int r = g_edit_key(&s->password, e);
            if (r == 2) password_done(w, 1);
            if (r == 3) password_done(w, 0);
        } else if (e->type == EV_COMMAND && (e->id == B_OK || e->id == B_CANCEL)) password_done(w, e->id == B_OK);
        g_invalidate(w);
        return;
    }
    if (e->type == EV_COMMAND) {
        if (e->id >= B_CATEGORY && e->id < B_CATEGORY + CATEGORY_COUNT) { s->category = e->id - B_CATEGORY; s->selected = 0; s->top = 0; filter(w); }
        else if (e->id >= B_ROW && e->id < B_ROW + 64) { s->selected = s->top + e->id - B_ROW; }
        else act(w, e->id);
    } else if (e->type == EV_MENU) act(w, e->id);
    else if (e->type == EV_DOUBLE) {
        if (e->x >= lx && e->x < lx + lw && e->y >= ly && e->y < ly + lh) {
            int i = s->top + (e->y - ly - 2) / ROW_H;
            if (i >= 0 && i < s->shown_count) { s->selected = i; Package *p = current(w); act(w, p && p->installed[0] && !upgradable(p) ? B_RUN : B_INSTALL); }
        }
    } else if (e->type == EV_WHEEL) {
        s->top += e->id * 2;
        if (s->top > s->shown_count - rows) s->top = s->shown_count - rows;
        if (s->top < 0) s->top = 0;
        if (s->selected < s->top) s->selected = s->top;
        if (s->selected >= s->top + rows) s->selected = s->top + rows - 1;
    } else if (e->type == EV_KEY) {
        if (e->code == BGX_KEY_UP && s->selected > 0) { s->selected--; return; }
        if (e->code == BGX_KEY_DOWN && s->selected + 1 < s->shown_count) { s->selected++; return; }
        if (e->code == 0xc9) { s->selected -= rows; if (s->selected < 0) s->selected = 0; return; }
        if (e->code == 0xd1) { s->selected += rows; if (s->selected >= s->shown_count) s->selected = s->shown_count - 1; return; }
        if (e->code == BGX_KEY_ENTER) { Package *p = current(w); if (p && (!p->installed[0] || upgradable(p))) act(w, B_INSTALL); return; }
        if (e->code == 0x3f) { act(w, B_REFRESH); return; }   /* F5 */
        int r = g_edit_key(&s->search, e);
        if (r == 1 || r == 3) { if (r == 3) g_edit_set(&s->search, ""); s->selected = 0; s->top = 0; filter(w); }
    }
    g_invalidate(w);
}
static const GMenu menus[] = {
    {"Packages", {{"Refresh the list   F5", M_REFRESH}, {"Update everything", M_UPGRADE}, {"-", 0}, {"Repository...", M_REPO},
                  {"-", 0}, {"Close", M_CLOSE}}},
    {"Help", {{"About BarnStore", M_ABOUT}}},
};
const GApp app_store = {"BarnStore", "Install and update programs (brm)", ICON_STORE, 760, 540, 560, 420,
                        APP_RESIZABLE | APP_SINGLE, menus, 2, sizeof(State), open, event, tick, paint, 0};
