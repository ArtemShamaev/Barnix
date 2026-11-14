/* Gouse Prompt: a command window with its own built-in commands. Every
 * other program runs inside the window (ABI 13 run(); with ABI 14 run_area
 * the kernel draws its text console or picture into this window while
 * Happy Gouse waits, and hands back what it printed). "full" still gives
 * a program the whole screen, as DOS programs had in Windows 1.
 * Mouse drag selects text, Ctrl+C copies it (or the typed line), Ctrl+V or
 * the right button pastes, Tab completes commands and file names. */
#include "gouse.h"

#define LINES 400
#define WIDTH 100
#define HISTORY 32
#define CONSOLE_W (80 * 8 + 8)   /* a program's 80x25 text console */
#define CONSOLE_H (25 * 16 + 8)
typedef struct {
    char lines[LINES][WIDTH * 2];   /* WIDTH characters; Cyrillic takes two bytes */
    int count, scroll;
    GEdit input;
    char history[HISTORY][128];
    int history_count, history_at;
    char cwd[256];
    int selecting, has_selection, sel_line[2], sel_col[2];   /* [0] anchor, [1] end */
    char pending[256];   /* a command given when the window opened */
    char sudo_line[256];  /* waits for the root password */
    int asking_password, as_root;
    char password[64]; unsigned int password_at;   /* remembered for 5 minutes, as sudo does */
} State;
#define S ((State *)w->state)

static void out(State *s, const char *text)
{
    text = g_tr(text);   /* fixed messages are translated; lines are drawn as they are */
    s->has_selection = 0;
    /* wrap long output into several lines */
    do {
        if (s->count == LINES) { for (int i = 1; i < LINES; i++) g_copy(s->lines[i - 1], s->lines[i], WIDTH * 2); s->count--; }
        int n = 0, columns = 0;
        while (text[n] && text[n] != '\n' && n < WIDTH * 2 - 4) {
            if (((unsigned char)text[n] & 0xc0) != 0x80 && ++columns > WIDTH - 1) break;
            n++;
        }
        for (int i = 0; i < n; i++) s->lines[s->count][i] = text[i] == '\t' ? ' ' : text[i];
        s->lines[s->count][n] = 0;
        s->count++;
        text += n;
        if (*text == '\n') text++;
    } while (*text);
    s->scroll = 0;
}
static void outf(State *s, const char *format, ...)
{
    char text[WIDTH * 3];
    va_list args; va_start(args, format);
    g_vformat(text, sizeof(text), format, args);
    va_end(args);
    out(s, text);
}
/* Absolute path with "." and ".." resolved. */
static void resolve(State *s, const char *path, char *result, int capacity)
{
    char joined[512];
    g_path_join(joined, sizeof(joined), s->cwd, path);
    char parts[32][64]; int count = 0;
    for (const char *p = joined; *p;) {
        while (*p == '/') p++;
        int n = 0; char part[64];
        while (*p && *p != '/' && n < 63) part[n++] = *p++;
        while (*p && *p != '/') p++;
        part[n] = 0;
        if (!n || g_equal(part, ".")) continue;
        if (g_equal(part, "..")) { if (count) count--; continue; }
        if (count < 32) g_copy(parts[count++], part, 64);
    }
    g_copy(result, "/", capacity);
    for (int i = 0; i < count; i++) { if (i) g_append(result, "/", capacity); g_append(result, parts[i], capacity); }
}
static void prompt_text(State *s, char *out_text, int capacity) { g_format(out_text, capacity, "%s> ", s->cwd); }

/* ---------------------------------------------------------------- running programs */
static void show_output(State *s, char *output, int n)
{
    char *text = output;
    for (int i = 0; i < n; i++) if (output[i] == '\f') { s->count = 0; text = output + i + 1; }
    for (char *p = text; *p;) {   /* line by line, as printed */
        char *e = p;
        while (*e && *e != '\n' && *e != '\f') e++;
        char saved = *e; *e = 0;
        if (e > p && e[-1] == '\r') e[-1] = 0;
        if (*p || saved) out(s, p[0] ? p : " ");
        *e = saved;
        p = *e ? e + 1 : e;
    }
}
/* Runs a program in the window. */
static void run_inside(GWindow *w, const char *path, int argc, char **argv)
{
    State *s = S;
    static void *swap;
    static char *output;
    if (barnix->version < 13 || !barnix->run) { outf(s, "This kernel cannot run programs in a window. Type: full %s", argv[0]); return; }
    if (!swap) swap = g_alloc(APP_STACK_TOP - APP_LOAD_MIN);
    if (!output) output = g_alloc(65536);
    if (!swap || !output) { out(s, "Not enough memory to run the program here."); return; }
    /* Room for the program's 80x25 console, when the screen has it. */
    if (w->w < CONSOLE_W || w->h < CONSOLE_H)
        g_resize(w, w->w < CONSOLE_W ? CONSOLE_W : w->w, w->h < CONSOLE_H ? CONSOLE_H : w->h);
    out(s, "(running...)");
    g_set_title(w, "Gouse Prompt - running");
    g_invalidate(w);
    g_flush();   /* this frame is what the program is drawn over */
    s->count--;  /* the "running" line goes again */
    barnix->cd(s->cwd);
    if (barnix->version >= 14) {
        barnix->run_area(w->x, w->y, w->w, w->h);
        if (s->as_root && barnix->sudo(s->password)) { out(s, "sudo: authentication failed"); s->password_at = 0; s->as_root = 0; return; }
    }
    s->as_root = 0;
    int status = 0;
    int n = barnix->run(path, argc, (const char *const *)argv, output, 65536, swap, &status);
    bgx_keys_release();
    char cwd[256];
    if (barnix->getcwd(cwd, sizeof(cwd)) >= 0) g_copy(s->cwd, cwd, sizeof(s->cwd));
    g_set_title(w, "Gouse Prompt");
    g_invalidate(w);
    if (n < 0) { outf(s, "%s cannot be run (missing, not a program, or access denied).", path); g_sound(G_SOUND_ERROR); return; }
    show_output(s, output, n);
    if (status) outf(s, "(exit status %d)", status);
}
static int program_path(State *s, const char *name, char *path, int capacity)
{
    int has_slash = 0;
    for (const char *p = name; *p; p++) if (*p == '/') has_slash = 1;
    if (has_slash) resolve(s, name, path, capacity);
    else {
        g_path_join(path, capacity, "/usr/bin", name);
        if (barnix->size(path) < 0) g_path_join(path, capacity, "/bin", name);
    }
    return barnix->size(path) >= 0;
}

/* ---------------------------------------------------------------- built-in commands */
static void command(GWindow *w, char *line)
{
    State *s = S;
    char *argv[16]; int argc = 0;
    for (char *p = line; *p && argc < 16;) {   /* words, "quoted words" */
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = 0;
            continue;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    const char *cmd = argv[0];
    char path[256], other[256];
    if (g_equal_ci(cmd, "help") || g_equal(cmd, "?")) {
        out(s, "Gouse Prompt commands:");
        out(s, "  dir [path]       list files          cd <path>        change folder");
        out(s, "  type <file>      show a text file    mkdir <name>     make a folder");
        out(s, "  copy <a> <b>     copy a file         move <a> <b>     move or rename");
        out(s, "  del <file>       delete a file       rmdir <name>     delete a folder");
        out(s, "  echo <text>      print text          cls              clear the window");
        out(s, "  date / time      show the clock      mem              memory and screen");
        out(s, "  whoami / ver     user and version    apps             Gouse applications");
        out(s, "  open <app>       open an application full <program>   run it full screen");
        out(s, "  exit             close this window");
        out(s, "  sudo <command>   run it as root");
        out(s, "Other commands (brm, gcc, macro, git, snake...) run right in this window.");
        out(s, "Mouse selects text: Ctrl+C copies, Ctrl+V or the right button pastes.");
        out(s, "Tab completes names, Up/Down recall commands, PageUp/PageDown scroll.");
    } else if (g_equal_ci(cmd, "cls") || g_equal_ci(cmd, "clear")) { s->count = 0; s->has_selection = 0; }
    else if (g_equal_ci(cmd, "ver")) outf(s, "Happy Gouse " GOUSE_VERSION " on Barnix (native ABI %u)", barnix->version);
    else if (g_equal_ci(cmd, "whoami")) out(s, barnix->current_user());
    else if (g_equal_ci(cmd, "echo")) {
        char text[WIDTH * 2] = "";
        for (int i = 1; i < argc; i++) { if (i > 1) g_append(text, " ", sizeof(text)); g_append(text, argv[i], sizeof(text)); }
        out(s, text);
    } else if (g_equal_ci(cmd, "date") || g_equal_ci(cmd, "time")) {
        GTime t; g_time(&t);
        outf(s, "%s %d %s %d   %02d:%02d:%02d", g_day(t.weekday), t.day, g_month_of(t.month), t.year, t.hour, t.minute, t.second);
    } else if (g_equal_ci(cmd, "mem")) {
        outf(s, "Gouse memory: %u KiB free of %u KiB", g_memory_free() >> 10, g_memory_total() >> 10);
        outf(s, "Screen: %d x %d", g_screen_w, g_screen_h);
    } else if (g_equal_ci(cmd, "pwd")) out(s, s->cwd);
    else if (g_equal_ci(cmd, "dir") || (g_equal_ci(cmd, "ls") && argc <= 2 && (argc < 2 || argv[1][0] != '-'))) {
        resolve(s, argc > 1 ? argv[1] : ".", path, sizeof(path));
        unsigned int cursor = 0; BarnixDirEntry entry; int result, files = 0, dirs = 0;
        outf(s, " Folder %s", path);
        while ((result = barnix->directory(path, &cursor, &entry)) > 0) {
            if (g_equal(entry.name, ".") || g_equal(entry.name, "..")) continue;
            char full[320]; g_path_join(full, sizeof(full), path, entry.name);
            if (entry.kind == 4) { outf(s, "  <DIR>          %s", entry.name); dirs++; }
            else { outf(s, "  %14d  %s", barnix->size(full), entry.name); files++; }
        }
        if (result < 0) out(s, result == -13 ? "Access denied: only root can list this folder (programs still run by name)." : "Cannot read this folder.");
        else outf(s, "  %d file(s), %d folder(s)", files, dirs);
    } else if (g_equal_ci(cmd, "cd") || g_equal_ci(cmd, "chdir")) {
        if (argc < 2) { out(s, s->cwd); return; }
        resolve(s, argv[1], path, sizeof(path));
        unsigned int cursor = 0; BarnixDirEntry entry;
        if (barnix->directory(path, &cursor, &entry) < 0) out(s, "No such folder.");
        else g_copy(s->cwd, path, sizeof(s->cwd));
    } else if ((g_equal_ci(cmd, "type") || g_equal_ci(cmd, "cat")) && argc == 2) {
        static char data[8192];
        resolve(s, argv[1], path, sizeof(path));
        int n = g_read_file(path, data, sizeof(data) - 1);
        if (n < 0) { out(s, "Cannot read the file."); return; }
        data[n] = 0;
        out(s, data);
        if (barnix->size(path) > n) out(s, "(only the first 8 KiB are shown)");
    } else if (g_equal_ci(cmd, "mkdir") || g_equal_ci(cmd, "md")) {
        if (argc < 2) { out(s, "mkdir <name>"); return; }
        resolve(s, argv[1], path, sizeof(path));
        if (barnix->mkdir(path)) out(s, "Cannot make the folder.");
    } else if (g_equal_ci(cmd, "rmdir") || g_equal_ci(cmd, "rd")) {
        if (argc < 2) { out(s, "rmdir <name>"); return; }
        resolve(s, argv[1], path, sizeof(path));
        if (barnix->rmdir(path)) out(s, "Cannot delete the folder (is it empty?).");
    } else if (g_equal_ci(cmd, "del") || g_equal_ci(cmd, "rm") || g_equal_ci(cmd, "erase")) {
        if (argc < 2) { out(s, "del <file>"); return; }
        resolve(s, argv[1], path, sizeof(path));
        if (barnix->rm(path)) out(s, "Cannot delete the file.");
    } else if (g_equal_ci(cmd, "copy") || g_equal_ci(cmd, "cp") || g_equal_ci(cmd, "move") || g_equal_ci(cmd, "mv") ||
               g_equal_ci(cmd, "ren") || g_equal_ci(cmd, "rename")) {
        if (argc < 3) { outf(s, "%s <from> <to>", cmd); return; }
        resolve(s, argv[1], path, sizeof(path)); resolve(s, argv[2], other, sizeof(other));
        int copy = g_equal_ci(cmd, "copy") || g_equal_ci(cmd, "cp");
        if (copy ? barnix->cp(path, other) : barnix->mv(path, other)) out(s, "The operation failed.");
        else out(s, copy ? "        1 file(s) copied." : "        1 file(s) moved.");
    } else if (g_equal_ci(cmd, "apps")) {
        for (int i = 0; i < g_app_count; i++) outf(s, "  %s - %s", g_apps[i]->name, g_apps[i]->description);
    } else if (g_equal_ci(cmd, "open") || g_equal_ci(cmd, "start")) {
        char name[64] = "";
        for (int i = 1; i < argc; i++) { if (i > 1) g_append(name, " ", sizeof(name)); g_append(name, argv[i], sizeof(name)); }
        const GApp *app = g_app_named(name);
        if (app) g_open(app, 0);
        else if (argc > 1 && program_path(s, argv[1], path, sizeof(path))) run_inside(w, path, argc - 1, argv + 1);
        else out(s, "No such application. Type apps for the list.");
    } else if (g_equal_ci(cmd, "run")) {
        if (argc < 2) { out(s, "run <program>"); return; }
        if (program_path(s, argv[1], path, sizeof(path))) run_inside(w, path, argc - 1, argv + 1);
        else { outf(s, "Unknown program: %s", argv[1]); g_sound(G_SOUND_ERROR); }
    } else if (g_equal_ci(cmd, "full") || g_equal_ci(cmd, "fullscreen")) {
        if (argc < 2) { out(s, "full <program>"); return; }
        if (program_path(s, argv[1], path, sizeof(path))) g_run_full(w, path);
        else { outf(s, "Unknown program: %s", argv[1]); g_sound(G_SOUND_ERROR); }
    } else if (g_equal_ci(cmd, "sudo")) {
        if (argc < 2) { out(s, "sudo <command>: run a program as root"); return; }
        char rest[256] = "";
        for (int i = 1; i < argc; i++) { if (i > 1) g_append(rest, " ", sizeof(rest)); g_append(rest, argv[i], sizeof(rest)); }
        if (g_equal(barnix->current_user(), "root")) { command(w, rest); return; }
        if (barnix->version < 14) { out(s, "This kernel has no sudo for windows: run sudo in the shell."); return; }
        if (s->password_at && g_now() - s->password_at < 300000) { s->as_root = 1; command(w, rest); s->as_root = 0; return; }
        g_copy(s->sudo_line, rest, sizeof(s->sudo_line));
        s->asking_password = 1;
        out(s, "[sudo] root password:");
    } else if (g_equal_ci(cmd, "su")) {
        out(s, "su works in the Barnix shell; here use sudo <command>.");
    } else if (g_equal_ci(cmd, "exit")) { g_close(w); }
    else if (program_path(s, cmd, path, sizeof(path))) run_inside(w, path, argc, argv);
    else { outf(s, "Unknown command: %s (type help)", cmd); g_sound(G_SOUND_ERROR); }
}

/* ---------------------------------------------------------------- selection and clipboard */
static int rows_of(GWindow *w) { return (w->h - 8) / 16; }
static int first_line(GWindow *w)
{
    State *s = S;
    int first = s->count - (rows_of(w) - 1) - s->scroll;
    return first < 0 ? 0 : first;
}
static int byte_at(const char *line, int column)
{
    int i = 0, c = 0, n = g_len(line);
    while (i < n && c < column) { i = g_utf8_next(line, n, i); c++; }
    return i;
}
static void ordered(State *s, int *l0, int *c0, int *l1, int *c1)
{
    int a = s->sel_line[0] < s->sel_line[1] || (s->sel_line[0] == s->sel_line[1] && s->sel_col[0] <= s->sel_col[1]) ? 0 : 1;
    *l0 = s->sel_line[a]; *c0 = s->sel_col[a]; *l1 = s->sel_line[1 - a]; *c1 = s->sel_col[1 - a];
}
static void copy_selection(State *s)
{
    static char text[16384];
    int l0, c0, l1, c1, n = 0;
    ordered(s, &l0, &c0, &l1, &c1);
    for (int l = l0; l <= l1 && l < s->count; l++) {
        const char *line = s->lines[l];
        int from = l == l0 ? byte_at(line, c0) : 0, to = l == l1 ? byte_at(line, c1) : g_len(line);
        for (int i = from; i < to && n < (int)sizeof(text) - 2; i++) text[n++] = line[i];
        if (l < l1 && n < (int)sizeof(text) - 2) text[n++] = '\n';
    }
    g_clipboard_set(text, n);
}
static void paste(GWindow *w)
{
    State *s = S;
    g_clipboard_sync();
    /* the first line goes into the input; more lines run one by one */
    for (int i = 0; i < g_clipboard_len; i++) {
        char c = g_clipboard[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (i + 1 >= g_clipboard_len) break;
            char line[256]; g_copy(line, s->input.text, sizeof(line));
            char prompt[300]; prompt_text(s, prompt, sizeof(prompt)); g_append(prompt, line, sizeof(prompt));
            out(s, prompt);
            g_edit_set(&s->input, "");
            command(w, line);
            continue;
        }
        int len = g_len(s->input.text);
        if (len + 1 >= (int)sizeof(s->input.text)) break;
        for (int k = len; k >= s->input.cursor; k--) s->input.text[k + 1] = s->input.text[k];
        s->input.text[s->input.cursor++] = c == '\t' ? ' ' : c;
    }
}

/* ---------------------------------------------------------------- Tab completion */
static void complete(State *s)
{
    char *text = s->input.text;
    int end = s->input.cursor, start = end;
    while (start > 0 && text[start - 1] != ' ') start--;
    char word[128]; int n = end - start < 127 ? end - start : 127;
    for (int i = 0; i < n; i++) word[i] = text[start + i];
    word[n] = 0;
    int first_word = 1;
    for (int i = 0; i < start; i++) if (text[i] != ' ') first_word = 0;
    /* folder to look in and the beginning of the name */
    char folder[256], prefix[128];
    int slash = -1;
    for (int i = 0; word[i]; i++) if (word[i] == '/') slash = i;
    if (slash >= 0) {
        char dir[128]; for (int i = 0; i <= slash; i++) dir[i] = word[i];
        dir[slash + 1] = 0;
        resolve(s, dir, folder, sizeof(folder));
        g_copy(prefix, word + slash + 1, sizeof(prefix));
    } else {
        if (first_word) g_copy(folder, "/usr/bin", sizeof(folder)); else g_copy(folder, s->cwd, sizeof(folder));
        g_copy(prefix, word, sizeof(prefix));
    }
    char match[256] = ""; int matches = 0, kind = 0, plen = g_len(prefix);
    char list[WIDTH * 2] = "";
    unsigned int cursor = 0; BarnixDirEntry entry;
    while (barnix->directory(folder, &cursor, &entry) > 0) {
        if (entry.name[0] == '.' && prefix[0] != '.') continue;
        int ok = 1;
        for (int i = 0; i < plen; i++) if (entry.name[i] != prefix[i]) { ok = 0; break; }
        if (!ok) continue;
        if (!matches) { g_copy(match, entry.name, sizeof(match)); kind = entry.kind; }
        else {   /* keep the common beginning */
            int i = 0; while (match[i] && match[i] == entry.name[i]) i++;
            match[i] = 0;
        }
        if (matches < 12) { g_append(list, entry.name, sizeof(list)); g_append(list, entry.kind == 4 ? "/  " : "  ", sizeof(list)); }
        matches++;
    }
    if (!matches) { g_sound(G_SOUND_ERROR); return; }
    if (matches > 1 && g_len(match) == plen) { out(s, list); return; }
    char added[160]; g_copy(added, match + plen, sizeof(added));
    if (matches == 1) g_append(added, kind == 4 ? "/" : " ", sizeof(added));
    int len = g_len(text), add = g_len(added);
    if (len + add >= (int)sizeof(s->input.text)) return;
    for (int k = len; k >= end; k--) text[k + add] = text[k];
    for (int i = 0; i < add; i++) text[end + i] = added[i];
    s->input.cursor += add;
}

/* ---------------------------------------------------------------- window */
static void open(GWindow *w, const char *argument)
{
    if (barnix->getcwd(S->cwd, sizeof(S->cwd)) < 0) g_copy(S->cwd, "/", sizeof(S->cwd));
    out(S, "Happy Gouse Prompt " GOUSE_VERSION ".  Type help for commands.");
    out(S, "");
    if (argument && argument[0]) g_copy(S->pending, argument, sizeof(S->pending));
}
static void tick(GWindow *w, int dt)
{
    (void)dt;
    State *s = S;
    if (!s->pending[0]) return;
    char line[256], prompt[300];
    g_copy(line, s->pending, sizeof(line));
    s->pending[0] = 0;
    prompt_text(s, prompt, sizeof(prompt)); g_append(prompt, line, sizeof(prompt));
    out(s, prompt);
    command(w, line);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, 0x0c0f14);
    int rows = rows_of(w), input_row = rows - 1;
    int first = first_line(w);
    int l0 = 0, c0 = 0, l1 = -1, c1 = 0;
    if (s->has_selection) ordered(s, &l0, &c0, &l1, &c1);
    for (int r = 0; r < input_row && first + r < s->count; r++) {
        int l = first + r;
        if (l >= l0 && l <= l1) {   /* selected part */
            int len = 0; for (const char *p = s->lines[l]; *p; p++) if (((unsigned char)*p & 0xc0) != 0x80) len++;
            int from = l == l0 ? c0 : 0, to = l == l1 ? c1 : len + 1;
            if (to > from) bgx_fill(c, 4 + from * 8, 4 + r * 16, (to - from) * 8, 16, 0x2f5f9f);
        }
        g_text_raw(c, 4, 4 + r * 16, s->lines[l], 0xc8d0dc);
    }
    int y = 4 + (s->count - first < input_row ? s->count - first : input_row) * 16;
    char prompt[300]; prompt_text(s, prompt, sizeof(prompt));
    if (s->asking_password) g_copy(prompt, "Password: ", sizeof(prompt));
    int px = 4 + g_text_raw(c, 4, y, prompt, 0x5cf07a);
    if (s->asking_password) {
        char stars[130]; int n = 0;
        for (const char *p = s->input.text; *p && n < 128; p++) if (((unsigned char)*p & 0xc0) != 0x80) stars[n++] = '*';
        stars[n] = 0;
        g_text_raw(c, px, y, stars, 0xffffff);
        if (g_focused(w) && (g_now() / 500) % 2 == 0) bgx_fill(c, px + n * 8, y + 13, 8, 3, 0x5cf07a);
        return;
    }
    /* the input line and a blinking block cursor */
    int before = 0;
    for (int i = 0; i < s->input.cursor; i++) if (((unsigned char)s->input.text[i] & 0xc0) != 0x80) before++;
    g_text_fit_raw(c, px, y, w->w - px - 4, s->input.text, 0xffffff);
    if (g_focused(w) && (g_now() / 500) % 2 == 0) bgx_fill(c, px + before * 8, y + 13, 8, 3, 0x5cf07a);
    if (s->scroll) g_text(c, w->w - 120, 4, "(scrolled back)", 0x707a88);
}
static void mouse_cell(GWindow *w, int x, int y, int *line, int *column)
{
    State *s = S;
    int r = (y - 4) / 16; if (r < 0) r = 0;
    *line = first_line(w) + r;
    if (*line >= s->count) *line = s->count - 1;
    if (*line < 0) *line = 0;
    *column = (x - 4 + 4) / 8; if (*column < 0) *column = 0;
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_WHEEL) {
        s->scroll -= e->id * 3;
        if (s->scroll < 0) s->scroll = 0;
        if (s->scroll > s->count) s->scroll = s->count;
        return;
    }
    if (e->type == EV_MOUSE_DOWN && e->button == 2) { paste(w); return; }
    if (e->type == EV_MOUSE_DOWN && e->button == 1 && s->count) {
        mouse_cell(w, e->x, e->y, &s->sel_line[0], &s->sel_col[0]);
        s->sel_line[1] = s->sel_line[0]; s->sel_col[1] = s->sel_col[0];
        s->selecting = 1; s->has_selection = 0;
        return;
    }
    if (e->type == EV_MOUSE_MOVE && s->selecting) {
        mouse_cell(w, e->x, e->y, &s->sel_line[1], &s->sel_col[1]);
        s->has_selection = s->sel_line[1] != s->sel_line[0] || s->sel_col[1] != s->sel_col[0];
        return;
    }
    if (e->type == EV_MOUSE_UP && s->selecting) { s->selecting = 0; if (s->has_selection) copy_selection(s); return; }
    if (e->type == EV_DOUBLE && s->count) {   /* a whole line */
        int line, column; mouse_cell(w, e->x, e->y, &line, &column);
        s->sel_line[0] = s->sel_line[1] = line; s->sel_col[0] = 0; s->sel_col[1] = WIDTH;
        s->has_selection = 1; copy_selection(s);
        return;
    }
    if (e->type != EV_KEY) return;
    int ctrl = g_ctrl();
    if (e->key == 3 || (ctrl && e->code == BGX_KEY_C)) {   /* Ctrl+C */
        if (s->has_selection) copy_selection(s);
        else if (s->input.text[0] && !s->asking_password) g_clipboard_set(s->input.text, g_len(s->input.text));
        return;
    }
    if (e->key == 22 || (ctrl && e->code == 0x2f)) { paste(w); return; }   /* Ctrl+V */
    if (e->key == 1 || (ctrl && e->code == BGX_KEY_A)) {                    /* Ctrl+A: everything */
        if (!s->count) return;
        s->sel_line[0] = 0; s->sel_col[0] = 0; s->sel_line[1] = s->count - 1; s->sel_col[1] = WIDTH;
        s->has_selection = 1; copy_selection(s);
        return;
    }
    if (e->key == 12 || (ctrl && e->code == 0x26)) { s->count = 0; s->has_selection = 0; return; }   /* Ctrl+L */
    if (e->code == BGX_KEY_TAB) { complete(s); return; }
    if (e->code == 0xc9) { s->scroll += 10; if (s->scroll > s->count) s->scroll = s->count; return; }
    if (e->code == 0xd1) { s->scroll -= 10; if (s->scroll < 0) s->scroll = 0; return; }
    if (e->code == BGX_KEY_UP || e->code == BGX_KEY_DOWN) {
        if (!s->history_count) return;
        s->history_at += e->code == BGX_KEY_UP ? -1 : 1;
        if (s->history_at < 0) s->history_at = 0;
        if (s->history_at >= s->history_count) { s->history_at = s->history_count; g_edit_set(&s->input, ""); return; }
        g_edit_set(&s->input, s->history[s->history_at]);
        return;
    }
    if (e->code == BGX_KEY_ESC) { g_edit_set(&s->input, ""); s->has_selection = 0; return; }
    if (s->asking_password) {
        int result = g_edit_key(&s->input, e);
        if (result == 3) { s->asking_password = 0; g_edit_set(&s->input, ""); out(s, "sudo: cancelled"); return; }
        if (result != 2) return;
        s->asking_password = 0;
        if (barnix->sudo(s->input.text)) { g_edit_set(&s->input, ""); out(s, "sudo: authentication failed"); g_sound(G_SOUND_ERROR); return; }
        g_copy(s->password, s->input.text, sizeof(s->password));
        s->password_at = g_now(); if (!s->password_at) s->password_at = 1;
        g_edit_set(&s->input, "");
        char line[256]; g_copy(line, s->sudo_line, sizeof(line));
        s->as_root = 1;
        command(w, line);
        s->as_root = 0;
        return;
    }
    if (g_edit_key(&s->input, e) != 2) return;
    char line[256], prompt[300];
    g_copy(line, s->input.text, sizeof(line));
    prompt_text(s, prompt, sizeof(prompt));
    g_append(prompt, line, sizeof(prompt));
    out(s, prompt);
    if (line[0] && (!s->history_count || !g_equal(s->history[s->history_count - 1], line))) {
        if (s->history_count == HISTORY) { for (int i = 1; i < HISTORY; i++) g_copy(s->history[i - 1], s->history[i], 128); s->history_count--; }
        g_copy(s->history[s->history_count++], line, 128);
    }
    s->history_at = s->history_count;
    g_edit_set(&s->input, "");
    command(w, line);
}
const GApp app_terminal = {"Gouse Prompt", "Commands, files and programs", ICON_TERMINAL, CONSOLE_W, CONSOLE_H, 300, 140,
                           APP_RESIZABLE, 0, 0, sizeof(State), open, event, tick, paint, 0};
