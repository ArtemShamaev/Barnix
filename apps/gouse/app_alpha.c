/* Alpha Launcher: starts the user's Minecraft Alpha (a1.2.6 client jar with
 * a jar mod on top) on BJVM, the Barnix Java machine. The game folder holds
 * mc.jar, mod.jar (optional) and, with user.home pointing at it, the game's
 * own data in minecraft/ (saves, options.txt, texture packs). The game gets
 * the whole screen and all memory: Happy Gouse closes meanwhile and comes
 * back when the game ends. Settings: ~/.alpha-launcher.cfg. */
#include "gouse.h"

enum { B_PLAY = 1, B_FOLDER, B_SAVES, B_MOD, B_MEMORY = 10, F_NAME = 30, F_DIR,
       M_PLAY = 40, M_FOLDER, M_SAVES, M_ABOUT, M_CLOSE };
#define WORLDS 8
static const int memories[] = {0, 256, 384, 512};   /* MiB of Java heap; 0 = automatic */
typedef struct {
    GEdit name, dir;
    int focus;          /* 0 name, 1 folder */
    int memory, with_mod;
    int has_game, has_mod, has_java;
    unsigned int game_size, mod_size;
    char worlds[WORLDS][64]; int world_count;
    char status[200];
} State;
#define S ((State *)w->state)

static void config_path(char *out, int capacity) { g_path_join(out, capacity, g_home(), ".alpha-launcher.cfg"); }
static void load(GWindow *w)
{
    State *s = S;
    g_edit_set(&s->name, "Player"); g_edit_set(&s->dir, "/mc");
    s->memory = 0; s->with_mod = 1;
    char path[256], text[512];
    config_path(path, sizeof(path));
    int n = g_read_file(path, text, sizeof(text) - 1);
    if (n < 0) return;
    text[n] = 0;
    for (char *line = text; *line;) {
        char *end = line; while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        if (line[0] == 'n' && line[1] == 'a' && line[4] == '=' && line[5]) g_edit_set(&s->name, line + 5);       /* name= */
        else if (line[0] == 'd' && line[3] == '=' && line[4]) g_edit_set(&s->dir, line + 4);                     /* dir= */
        else if (line[0] == 'm' && line[1] == 'e' && line[6] == '=') s->memory = g_atoi(line + 7) & 3;          /* memory= */
        else if (line[0] == 'm' && line[1] == 'o' && line[3] == '=') s->with_mod = line[4] != '0';              /* mod= */
        *end = saved; line = *end ? end + 1 : end;
    }
}
static void save(GWindow *w)
{
    State *s = S;
    char path[256], text[512];
    config_path(path, sizeof(path));
    int n = g_format(text, sizeof(text), "name=%s\ndir=%s\nmemory=%d\nmod=%d\n", s->name.text, s->dir.text, s->memory, s->with_mod);
    g_write_file(path, text, n);
}
static void join(State *s, char *out, int capacity, const char *name) { g_path_join(out, capacity, s->dir.text, name); }
/* What is in the game folder. */
static void scan(GWindow *w)
{
    State *s = S;
    char path[300];
    join(s, path, sizeof(path), "mc.jar");
    int size = barnix->size(path);
    s->has_game = size > 0; s->game_size = size > 0 ? (unsigned int)size : 0;
    join(s, path, sizeof(path), "mod.jar");
    size = barnix->size(path);
    s->has_mod = size > 0; s->mod_size = size > 0 ? (unsigned int)size : 0;
    s->has_java = barnix->size("/usr/bin/java") > 0;
    s->world_count = 0;
    join(s, path, sizeof(path), "minecraft/saves");
    unsigned int position = 0; BarnixDirEntry entry;
    while (s->world_count < WORLDS && barnix->directory(path, &position, &entry) > 0)
        if (entry.kind == 4 && entry.name[0] != '.') g_copy(s->worlds[s->world_count++], entry.name, 64);
    if (!s->has_java) g_copy(s->status, g_tr("Java (BJVM) is not installed: BarnStore, package java."), sizeof(s->status));
    else if (!s->has_game) g_format(s->status, sizeof(s->status), g_tr("No mc.jar in %s: copy the Alpha client jar there."), s->dir.text);
    else g_copy(s->status, g_tr("Ready. The game takes the whole screen; Gouse comes back after it."), sizeof(s->status));
}
static int valid_name(const char *name)
{
    int n = 0;
    for (; name[n]; n++) {
        char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return n >= 2 && n <= 16;
}
static void play(GWindow *w)
{
    State *s = S;
    scan(w);
    if (!s->has_java || !s->has_game) { g_error(w, "Alpha Launcher", s->status); return; }
    if (!valid_name(s->name.text)) { g_error(w, "Alpha Launcher", "The nickname is 2-16 letters, digits or _."); return; }
    save(w);
    char mc[300], mod[300], classpath[620], heap[24] = "", line[1024];
    join(s, mc, sizeof(mc), "mc.jar");
    join(s, mod, sizeof(mod), "mod.jar");
    /* the mod first: its classes replace the game's */
    if (s->with_mod && s->has_mod) g_format(classpath, sizeof(classpath), "%s:%s", mod, mc);
    else g_copy(classpath, mc, sizeof(classpath));
    if (memories[s->memory]) g_format(heap, sizeof(heap), "-Xmx%dm ", memories[s->memory]);
    g_format(line, sizeof(line), "/usr/bin/java %s-Duser.home=%s -cp %s net.minecraft.client.Minecraft %s",
             heap, s->dir.text, classpath, s->name.text);
    if (g_launch(line)) g_error(w, "Alpha Launcher", "Java cannot be started.");
}

static void open(GWindow *w, const char *argument)
{
    (void)argument;
    load(w);
    scan(w);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    /* a header: the icon and the title on a dark band */
    bgx_fill(c, 0, 0, w->w, 64, 0x2b3a1f);
    bgx_fill(c, 0, 60, w->w, 4, 0x5fb33a);
    g_icon(c, 14, 14, ICON_ALPHA, 32);
    bgx_text(c, 58, 12, "Minecraft Alpha", 0xffffff, 2);
    g_text(c, 60, 42, s->has_mod && s->with_mod ? "a1.2.6 with your mod, on BJVM" : "a1.2.6, on BJVM", 0xc8e6b4);
    int y = 78, lx = 16, fx = 150, fw = w->w - fx - 16;
    g_text(c, lx, y + 3, "Nickname:", g_theme.text);
    g_edit_draw(c, fx, y, fw, &s->name, g_focused(w) && s->focus == 0);
    g_zone(w, F_NAME, fx, y, fw, 22);
    y += 30;
    g_text(c, lx, y + 3, "Game folder:", g_theme.text);
    g_edit_draw(c, fx, y, fw, &s->dir, g_focused(w) && s->focus == 1);
    g_zone(w, F_DIR, fx, y, fw, 22);
    y += 32;
    g_text(c, lx, y + 5, "Java memory:", g_theme.text);
    static const char *const labels[] = {"Auto", "256 MiB", "384 MiB", "512 MiB"};
    for (int i = 0; i < 4; i++) g_toggle(w, c, B_MEMORY + i, fx + i * 84, y, 80, 24, labels[i], s->memory == i);
    y += 32;
    g_check(w, c, B_MOD, lx, y + 3, s->has_mod ? "Load mod.jar over the game" : "No mod.jar in the folder", s->with_mod && s->has_mod);
    y += 30;
    /* what was found */
    char text[160];
    g_panel(c, lx, y, w->w - 32, 76, g_theme.window, 1);
    if (s->has_game) g_format(text, sizeof(text), g_tr("mc.jar: %u KiB"), s->game_size / 1024); else g_copy(text, g_tr("mc.jar: missing"), sizeof(text));
    g_text(c, lx + 8, y + 6, text, s->has_game ? g_theme.text : 0xc03030);
    if (s->has_mod) g_format(text, sizeof(text), g_tr("mod.jar: %u KiB"), s->mod_size / 1024); else g_copy(text, g_tr("mod.jar: none (plain Alpha)"), sizeof(text));
    g_text(c, lx + 8, y + 24, text, g_theme.text);
    char worlds[200] = "";
    for (int i = 0; i < s->world_count; i++) { if (i) g_append(worlds, ", ", sizeof(worlds)); g_append(worlds, s->worlds[i], sizeof(worlds)); }
    g_format(text, sizeof(text), g_tr("Worlds: %s"), s->world_count ? worlds : g_tr("none yet"));
    g_text_fit(c, lx + 8, y + 42, w->w - 48, text, g_theme.shadow);
    g_text_fit(c, lx + 8, y + 58, w->w - 48, s->status, g_theme.shadow);
    /* buttons */
    int by = w->h - 48;
    g_button(w, c, B_FOLDER, lx, by + 6, 130, 28, "Game folder");
    g_button(w, c, B_SAVES, lx + 136, by + 6, 130, 28, "Worlds");
    g_button(w, c, B_PLAY, w->w - 176, by, 160, 40, "Play");
}
static void act(GWindow *w, int id)
{
    State *s = S;
    char path[300];
    if (id == B_PLAY || id == M_PLAY) play(w);
    else if (id == B_FOLDER || id == M_FOLDER) g_open(g_app_named("Executive"), s->dir.text);
    else if (id == B_SAVES || id == M_SAVES) { join(s, path, sizeof(path), "minecraft/saves"); g_open(g_app_named("Executive"), path); }
    else if (id == B_MOD) { s->with_mod = !s->with_mod; save(w); }
    else if (id >= B_MEMORY && id < B_MEMORY + 4) { s->memory = id - B_MEMORY; save(w); }
    else if (id == F_NAME || id == F_DIR) s->focus = id - F_NAME;
    else if (id == M_ABOUT) g_message(w, "Alpha Launcher", "Starts Minecraft Alpha a1.2.6 and your jar mod\non BJVM, the Java machine of Barnix.\nPut mc.jar and mod.jar into the game folder.");
    else if (id == M_CLOSE) g_close(w);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_COMMAND || e->type == EV_MENU) act(w, e->id);
    else if (e->type == EV_KEY) {
        if (e->code == BGX_KEY_TAB) { s->focus = !s->focus; return; }
        GEdit *field = s->focus ? &s->dir : &s->name;
        int r = g_edit_key(field, e);
        if (r == 2) play(w);
        else if (r == 1 && s->focus) scan(w);
    }
    g_invalidate(w);
}
static const GMenu menus[] = {
    {"Game", {{"Play", M_PLAY}, {"Game folder", M_FOLDER}, {"Worlds", M_SAVES}, {"-", 0}, {"Close", M_CLOSE}}},
    {"Help", {{"About the launcher", M_ABOUT}}},
};
const GApp app_alpha = {"Alpha Launcher", "Minecraft Alpha with your mod", ICON_ALPHA, 540, 400, 480, 380,
                        APP_SINGLE, menus, 2, sizeof(State), open, event, 0, paint, 0};
