/* Barnix Code: a code editor in the manner of Visual Studio Code, for Happy
 * Gouse. Activity bar (Explorer, Search, Run), a workspace folder tree,
 * tabs, the Dark+ colors, a minimap, Quick Open (Ctrl+P), the Command
 * Palette (Ctrl+Shift+P), find and replace (Ctrl+F, Ctrl+H), search in
 * files (Ctrl+Shift+F), and a bottom panel with Problems, Output and an
 * integrated Terminal in which programs run (ABI 14 run_area). F5 runs the
 * file by its language: C/C++ are built with GCC first, Python runs on
 * python3, HTML opens in Gouse Web, Nova JARs on BJVM.
 * Memory comes once from Gouse (g_alloc does not free): the documents and
 * lists below are shared by the (single) window. */
#include "gouse.h"
#include "code_symbols.inc"

#define DOCS 12
#define TEXT_MAX (128 * 1024)
#define UNDO 6
#define TREE_MAX 400
#define FILES_MAX 1200
#define RESULTS_MAX 300
#define PROBLEMS_MAX 120
#define TERM_LINES 300
#define TERM_WIDTH 160
#define ROW 20

/* ---------------------------------------------------------------- colors (Dark+) */
enum { C_EDITOR = 0x1e1e1e, C_SIDEBAR = 0x252526, C_ACTIVITY = 0x333333, C_TAB = 0x2d2d2d, C_BORDER = 0x3c3c3c,
       C_TEXT = 0xd4d4d4, C_DIM = 0x858585, C_BRIGHT = 0xffffff, C_STATUS = 0x007acc, C_SELECTED = 0x094771,
       C_HOVER = 0x2a2d2e, C_INPUT = 0x3c3c3c, C_ACCENT = 0x0e639c, C_ERROR = 0xf14c4c, C_WARNING = 0xcca700,
       C_HEADER = 0xbbbbbb };
static const unsigned int dark_palette[GTE_PAL_COUNT] = {
    0x1e1e1e, 0xd4d4d4, 0x1e1e1e, 0x6e7681, 0xc6c6c6, 0x264f78, 0xaeafad, 0x2a2d2e, 0x888888,
    0x569cd6, 0x4ec9b0, 0xce9178, 0x6a9955, 0xb5cea8, 0xc586c0, 0xdcdcaa};

/* ---------------------------------------------------------------- languages */
enum { L_HASH = 1, L_MARKUP = 2, L_CSS = 4, L_MARKDOWN = 8, L_TRIPLE = 16, L_COLON = 32, L_BACKTICK = 64 };
typedef struct {
    const char *name, *exts, *line_comment, *block_open, *block_close;
    const char *const *keywords, *const *types;
    int flags;
} Lang;
static const char *const kw_c[] = {"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
    "return", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "extern", "const", "volatile", "inline",
    "register", "restrict", "true", "false", "NULL", "nullptr", "class", "public", "private", "protected", "virtual",
    "override", "namespace", "using", "template", "typename", "new", "delete", "this", "operator", "try", "catch",
    "throw", "noexcept", "constexpr", "auto", "explicit", "friend", "static_cast", "dynamic_cast", 0};
static const char *const ty_c[] = {"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool",
    "size_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "FILE", "string",
    "vector", "map", "set", "std", "wchar_t", 0};
static const char *const kw_py[] = {"if", "elif", "else", "for", "in", "while", "break", "continue", "pass", "return", "def",
    "class", "import", "from", "as", "with", "try", "except", "finally", "raise", "lambda", "yield", "global", "nonlocal",
    "assert", "del", "is", "and", "or", "not", "None", "True", "False", "async", "await", 0};
static const char *const ty_py[] = {"self", "int", "float", "str", "list", "dict", "set", "tuple", "bool", "bytes", "object",
    "Exception", 0};
static const char *const kw_nova[] = {"if", "elif", "else", "for", "in", "while", "break", "continue", "pass", "return",
    "class", "init", "super", "this", "new", "null", "true", "false", "and", "or", "not", "var", 0};
static const char *const ty_nova[] = {"int", "double", "bool", "string", "void", 0};
static const char *const kw_js[] = {"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
    "return", "function", "var", "let", "const", "new", "delete", "typeof", "instanceof", "in", "of", "class", "extends",
    "super", "this", "null", "undefined", "true", "false", "try", "catch", "finally", "throw", "async", "await", "import",
    "export", "from", "yield", 0};
static const char *const ty_js[] = {"Math", "JSON", "Array", "Object", "String", "Number", "Promise", "document", "window",
    "console", 0};
static const char *const kw_sh[] = {"if", "then", "else", "elif", "fi", "for", "in", "do", "done", "while", "case", "esac",
    "function", "return", "export", "local", "echo", "cd", 0};
static const char *const kw_json[] = {"true", "false", "null", 0};
static const char *const none[] = {0};
enum { LANG_TEXT, LANG_C, LANG_CPP, LANG_PY, LANG_NOVA, LANG_JS, LANG_HTML, LANG_CSS, LANG_JSON, LANG_MD, LANG_SH, LANG_COUNT };
static const Lang langs[LANG_COUNT] = {
    {"Plain Text", "", 0, 0, 0, none, none, 0},
    {"C", ".c.h", "//", "/*", "*/", kw_c, ty_c, L_HASH},
    {"C++", ".cpp.cc.cxx.hpp.hh", "//", "/*", "*/", kw_c, ty_c, L_HASH},
    {"Python", ".py", "#", 0, 0, kw_py, ty_py, L_TRIPLE | L_COLON},
    {"Nova", ".nova", "//", 0, 0, kw_nova, ty_nova, L_COLON},
    {"JavaScript", ".js.mjs", "//", "/*", "*/", kw_js, ty_js, L_BACKTICK},
    {"HTML", ".html.htm", 0, "<!--", "-->", none, none, L_MARKUP},
    {"CSS", ".css", 0, "/*", "*/", none, none, L_CSS},
    {"JSON", ".json", 0, 0, 0, kw_json, none, 0},
    {"Markdown", ".md", 0, 0, 0, none, none, L_MARKDOWN},
    {"Shell", ".sh", "#", 0, 0, kw_sh, none, 0},
};
static int lang_of(const char *path)
{
    const char *dot = 0;
    for (const char *p = path; *p; p++) { if (*p == '.') dot = p; if (*p == '/') dot = 0; }
    if (!dot) return LANG_TEXT;
    int n = g_len(dot);
    for (int l = 1; l < LANG_COUNT; l++)
        for (const char *e = langs[l].exts; *e;) {
            const char *f = e + 1; while (*f && *f != '.') f++;
            if (f - e == n) { int same = 1; for (int i = 0; i < n; i++) if ((e[i] | 32) != (dot[i] | 32)) same = 0; if (same) return l; }
            e = f;
        }
    return LANG_TEXT;
}

/* ---------------------------------------------------------------- syntax colors */
static int is_word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'; }
static int in_list(const char *word, int n, const char *const *list)
{
    for (int i = 0; list[i]; i++) {
        int k = 0;
        while (k < n && list[i][k] == word[k]) k++;
        if (k == n && !list[i][k]) return 1;
    }
    return 0;
}
static int starts(const char *s, int length, int at, const char *with)
{
    for (int i = 0; with[i]; i++) if (at + i >= length || s[at + i] != with[i]) return 0;
    return 1;
}
static int hl_lang;
/* state: 1 block comment, 2 markup comment, 3 / 4 triple quotes ''' / """ */
static void highlight(const char *s, int length, unsigned char *k, int *state)
{
    const Lang *L = &langs[hl_lang];
    int i = 0;
    if (L->flags & L_MARKDOWN) {
        int first = 0; while (first < length && s[first] == ' ') first++;
        if (first < length && s[first] == '#') { for (int j = first; j < length; j++) k[j] = GTE_KEYWORD; return; }
        if (first + 1 < length && (s[first] == '-' || s[first] == '*' || s[first] == '>') && s[first + 1] == ' ') k[first] = GTE_TYPE;
        for (; i < length; i++) {
            if (s[i] == '`') { k[i++] = GTE_STRING; while (i < length && s[i] != '`') k[i++] = GTE_STRING; if (i < length) k[i] = GTE_STRING; }
            else if (s[i] == '[') { while (i < length && s[i] != ')') k[i++] = GTE_FUNCTION; if (i < length) k[i] = GTE_FUNCTION; }
        }
        return;
    }
    if (L->flags & L_MARKUP) {
        while (i < length) {
            if (*state == 2) { if (starts(s, length, i, "-->")) { k[i] = k[i + 1] = k[i + 2] = GTE_COMMENT; i += 3; *state = 0; } else k[i++] = GTE_COMMENT; continue; }
            if (starts(s, length, i, "<!--")) { *state = 2; continue; }
            if (s[i] == '<') {   /* a tag: name, attributes, values */
                k[i++] = GTE_DIRECTIVE;
                if (i < length && s[i] == '/') k[i++] = GTE_DIRECTIVE;
                while (i < length && is_word(s[i])) k[i++] = GTE_KEYWORD;
                while (i < length && s[i] != '>') {
                    if (s[i] == '"' || s[i] == '\'') { char q = s[i]; k[i++] = GTE_STRING; while (i < length && s[i] != q) k[i++] = GTE_STRING; if (i < length) k[i++] = GTE_STRING; }
                    else if (is_word(s[i]) || s[i] == '-') k[i++] = GTE_TYPE;
                    else i++;
                }
                if (i < length) k[i++] = GTE_DIRECTIVE;
                continue;
            }
            if (s[i] == '&') { while (i < length && s[i] != ';' && s[i] != ' ') k[i++] = GTE_NUMBER; if (i < length && s[i] == ';') k[i++] = GTE_NUMBER; continue; }
            i++;
        }
        return;
    }
    int colon = -1;   /* CSS: "property:" inside braces */
    if (L->flags & L_CSS)
        for (int j = 0; j < length; j++) { if (s[j] == ':') { colon = j; break; } if (s[j] == '{' || s[j] == ';') break; }
    int first = 0; while (first < length && s[first] == ' ') first++;
    if ((L->flags & L_HASH) && first < length && s[first] == '#' && *state == 0) {
        for (int j = first; j < length; j++) k[j] = GTE_DIRECTIVE;
        for (int j = first; j < length; j++) if (s[j] == '<' || s[j] == '"') { for (; j < length; j++) k[j] = GTE_STRING; }
        return;
    }
    while (i < length) {
        if (*state == 1) {
            k[i] = GTE_COMMENT;
            if (L->block_close && starts(s, length, i, L->block_close)) { for (int j = 0; L->block_close[j]; j++) k[i + j] = GTE_COMMENT; i += g_len(L->block_close); *state = 0; continue; }
            i++; continue;
        }
        if (*state == 3 || *state == 4) {
            const char *q = *state == 3 ? "'''" : "\"\"\"";
            if (starts(s, length, i, q)) { k[i] = k[i + 1] = k[i + 2] = GTE_STRING; i += 3; *state = 0; } else k[i++] = GTE_STRING;
            continue;
        }
        char c = s[i];
        if (L->line_comment && starts(s, length, i, L->line_comment)) { for (; i < length; i++) k[i] = GTE_COMMENT; break; }
        if (L->block_open && starts(s, length, i, L->block_open)) { *state = 1; for (int j = 0; L->block_open[j]; j++) k[i + j] = GTE_COMMENT; i += g_len(L->block_open); continue; }
        if ((L->flags & L_TRIPLE) && (starts(s, length, i, "'''") || starts(s, length, i, "\"\"\""))) {
            *state = s[i] == '\'' ? 3 : 4; k[i] = k[i + 1] = k[i + 2] = GTE_STRING; i += 3; continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && (L->flags & L_BACKTICK))) {
            k[i++] = GTE_STRING;
            while (i < length && s[i] != c) { if (s[i] == '\\' && i + 1 < length) k[i++] = GTE_STRING; k[i++] = GTE_STRING; }
            if (i < length) k[i++] = GTE_STRING;
            continue;
        }
        if ((L->flags & L_CSS) && c == '#' && i + 1 < length && is_word(s[i + 1]) && colon >= 0 && i > colon) { while (i < length && is_word(s[i])) k[i++] = GTE_NUMBER; continue; }
        if (c >= '0' && c <= '9' && (i == 0 || !is_word(s[i - 1]))) { while (i < length && (is_word(s[i]) || s[i] == '.')) k[i++] = GTE_NUMBER; continue; }
        if (is_word(c) || ((L->flags & L_CSS) && c == '-')) {
            int start = i;
            while (i < length && (is_word(s[i]) || ((L->flags & L_CSS) && s[i] == '-'))) i++;
            int n = i - start, kind = 0;
            if (L->flags & L_CSS) kind = colon >= 0 && start < colon ? GTE_TYPE : colon < 0 ? GTE_KEYWORD : 0;
            else if (in_list(s + start, n, L->keywords)) kind = GTE_KEYWORD;
            else if (in_list(s + start, n, L->types)) kind = GTE_TYPE;
            else { int j = i; while (j < length && s[j] == ' ') j++; if (j < length && s[j] == '(' && hl_lang != LANG_TEXT) kind = GTE_FUNCTION; }
            for (int j = start; j < i; j++) k[j] = (unsigned char)kind;
            continue;
        }
        i++;
    }
}

/* ---------------------------------------------------------------- documents */
typedef struct {
    int used, lang, untitled;
    char path[256];
    char *text;
    GTextEdit edit;
    char *undo[UNDO], *redo[UNDO];
    int undo_len[UNDO], undo_cur[UNDO], redo_len[UNDO], redo_cur[UNDO], undo_n, redo_n;
    unsigned int undo_at;
    int breaks[64], break_count;   /* breakpoints, 1-based lines */
} Doc;
typedef struct { char path[256]; short depth; char dir, open; } Node;
typedef struct { short file; int line; char preview[96]; } Result;
typedef struct { char file[192]; int line, column, error; char message[160]; } Problem;
enum { SIDE_EXPLORER, SIDE_SEARCH, SIDE_RUN };
enum { PANEL_PROBLEMS, PANEL_OUTPUT, PANEL_TERMINAL };
enum { F_EDITOR, F_TREE, F_SEARCH, F_TERMINAL, F_FIND, F_REPLACE };
enum { OV_NONE, OV_QUICK, OV_COMMANDS, OV_GOTO };

static struct {
    int ready;
    Doc *docs[DOCS];
    Node *tree; int tree_count, tree_top, tree_selected;
    char (*files)[160]; int file_count, files_ready;
    Result *results; int result_count, result_top, result_selected;
    Problem *problems; int problem_count, problem_top;
    char (*term)[TERM_WIDTH * 2]; int term_count, term_scroll;
    char *output; int output_length, output_top;
    char *scratch;   /* files read by search in files, run output */
    void *swap;
    /* completion: the list under the cursor */
    struct { char name[64]; const char *detail; unsigned char kind; int score; } comp[60];
    int comp_count, comp_selected, comp_top, comp_start, comp_open;
} pool;

typedef struct {
    int active, next_untitled;
    char folder[256];
    int sidebar, side, panel, panel_tab, panel_h, focus;
    int overlay, overlay_selected, overlay_top; GEdit overlay_input;
    int overlay_items[400], overlay_count;
    int find_open, replace_open, match_case; GEdit find, replace;
    GEdit search; int search_case;
    GEdit terminal; char cwd[256]; char history[16][128]; int history_count, history_at;
    char status[160];
    int dragging_panel;
    int tab_x[DOCS], tab_w[DOCS], tabs_shown;
} State;
#define S ((State *)w->state)

static void *take(unsigned int bytes) { return g_alloc(bytes); }
static int pool_ready(void)
{
    if (pool.ready) return 1;
    pool.tree = take(sizeof(Node) * TREE_MAX);
    pool.files = take(sizeof(*pool.files) * FILES_MAX);
    pool.results = take(sizeof(Result) * RESULTS_MAX);
    pool.problems = take(sizeof(Problem) * PROBLEMS_MAX);
    pool.term = take(sizeof(*pool.term) * TERM_LINES);
    pool.output = take(32768);
    pool.scratch = take(256 * 1024);
    pool.swap = take(APP_STACK_TOP - APP_LOAD_MIN);
    pool.ready = pool.tree && pool.files && pool.results && pool.problems && pool.term && pool.output && pool.scratch && pool.swap;
    return pool.ready;
}
static Doc *doc_slot(int i)
{
    if (!pool.docs[i]) {
        Doc *d = take(sizeof(Doc));
        if (!d) return 0;
        d->text = take(TEXT_MAX);
        for (int k = 0; k < UNDO; k++) { d->undo[k] = take(TEXT_MAX); d->redo[k] = take(TEXT_MAX); }
        if (!d->text || !d->redo[UNDO - 1]) return 0;
        pool.docs[i] = d;
    }
    return pool.docs[i];
}
static Doc *active_doc(GWindow *w)
{
    int a = S->active;
    return a >= 0 && a < DOCS && pool.docs[a] && pool.docs[a]->used ? pool.docs[a] : 0;
}
static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}
static void doc_title(const Doc *d, char *out, int capacity)
{
    if (d->path[0]) g_copy(out, base_name(d->path), capacity);
    else g_format(out, capacity, "Untitled-%d", d->untitled);
}
static void set_status(GWindow *w, const char *text) { g_copy(S->status, g_tr(text), sizeof(S->status)); }

/* ---------------------------------------------------------------- undo */
static void copy_text(char *to, const char *from, int n) { for (int i = 0; i < n; i++) to[i] = from[i]; }
static void push(char **ring, int *len, int *cur, int *count, const Doc *d)
{
    if (*count == UNDO) {
        char *oldest = ring[0];
        for (int i = 1; i < UNDO; i++) { ring[i - 1] = ring[i]; len[i - 1] = len[i]; cur[i - 1] = cur[i]; }
        ring[UNDO - 1] = oldest; (*count)--;
    }
    copy_text(ring[*count], d->text, d->edit.length);
    len[*count] = d->edit.length; cur[*count] = d->edit.cursor; (*count)++;
}
static void snapshot(Doc *d, int force)
{
    unsigned int now = g_now();
    if (!force && d->undo_n && now - d->undo_at < 700) { d->undo_at = now; return; }
    d->undo_at = now;
    push(d->undo, d->undo_len, d->undo_cur, &d->undo_n, d);
    d->redo_n = 0;
}
static void restore(Doc *d, char **ring, int *len, int *cur, int *count, char **other, int *olen, int *ocur, int *ocount)
{
    if (!*count) return;
    push(other, olen, ocur, ocount, d);
    int n = --*count;
    int cursor = cur[n];
    gte_set(&d->edit, ring[n], len[n]);
    d->edit.cursor = cursor <= d->edit.length ? cursor : d->edit.length;
    d->edit.modified = 1; d->undo_at = 0;
}
static void undo(Doc *d) { restore(d, d->undo, d->undo_len, d->undo_cur, &d->undo_n, d->redo, d->redo_len, d->redo_cur, &d->redo_n); }
static void redo(Doc *d) { restore(d, d->redo, d->redo_len, d->redo_cur, &d->redo_n, d->undo, d->undo_len, d->undo_cur, &d->undo_n); }

/* ---------------------------------------------------------------- opening and saving */
static void save_session(GWindow *w);
static int open_file(GWindow *w, const char *path)
{
    for (int i = 0; i < DOCS; i++)
        if (pool.docs[i] && pool.docs[i]->used && g_equal(pool.docs[i]->path, path)) { S->active = i; return i; }
    int slot = -1;
    for (int i = 0; i < DOCS && slot < 0; i++) if (!pool.docs[i] || !pool.docs[i]->used) slot = i;
    if (slot < 0) { g_error(w, "Barnix Code", "Too many open files: close a tab first."); return -1; }
    Doc *d = doc_slot(slot);
    if (!d) { g_error(w, "Barnix Code", "Not enough memory for another file."); return -1; }
    int n = 0;
    gte_init(&d->edit, d->text, TEXT_MAX);   /* first: it clears the buffer */
    if (path[0]) {
        n = g_read_file(path, d->text, TEXT_MAX - 1);
        if (n < 0) {
            if (barnix->size(path) > TEXT_MAX - 1) { g_error(w, "Barnix Code", "This file is too large (128 KiB at most)."); return -1; }
            n = 0;   /* a new file: created on save */
        }
    }
    gte_set(&d->edit, d->text, n);
    d->edit.highlight = highlight; d->edit.gutter = 1; d->edit.palette = dark_palette;
    d->break_count = 0; d->edit.dots = d->breaks; d->edit.dot_count = 0; d->edit.stop_line = 0;
    g_copy(d->path, path, sizeof(d->path));
    d->lang = lang_of(path);
    d->untitled = path[0] ? 0 : ++S->next_untitled;
    d->undo_n = d->redo_n = 0;
    d->used = 1;
    S->active = slot; S->focus = F_EDITOR;
    save_session(w);
    return slot;
}
static int save_doc(GWindow *w, Doc *d)
{
    if (!d->path[0]) return -1;
    if (g_write_file(d->path, d->text, d->edit.length)) { g_error(w, "Barnix Code", "Cannot save the file (is the folder yours?)."); return -1; }
    d->edit.modified = 0;
    d->lang = lang_of(d->path);
    char text[200]; g_format(text, sizeof(text), g_tr("Saved %s"), d->path); g_copy(S->status, text, sizeof(S->status));
    return 0;
}
enum { P_SAVE_AS = 100, P_FOLDER, P_NEW_FILE, P_NEW_FOLDER, P_RENAME, C_DELETE, C_CLOSE = 120 };
static void ask_save_as(GWindow *w, Doc *d)
{
    char path[256];
    if (d->path[0]) g_copy(path, d->path, sizeof(path));
    else g_path_join(path, sizeof(path), S->folder, "main.py");
    g_prompt(w, P_SAVE_AS, "Save As", "File path:", path);
}
static void save_active(GWindow *w)
{
    Doc *d = active_doc(w);
    if (!d) return;
    if (!d->path[0]) ask_save_as(w, d); else save_doc(w, d);
}
static void close_doc(GWindow *w, int i, int force)
{
    Doc *d = pool.docs[i];
    if (!d || !d->used) return;
    if (d->edit.modified && !force) {
        char title[64], text[200]; doc_title(d, title, sizeof(title));
        g_format(text, sizeof(text), g_tr("%s has unsaved changes.\nClose it anyway?"), title);
        g_confirm(w, C_CLOSE + i, "Barnix Code", text);
        return;
    }
    d->used = 0;
    if (S->active == i) {
        S->active = -1;
        for (int k = i; k >= 0 && S->active < 0; k--) if (pool.docs[k] && pool.docs[k]->used) S->active = k;
        for (int k = i; k < DOCS && S->active < 0; k++) if (pool.docs[k] && pool.docs[k]->used) S->active = k;
    }
    save_session(w);
}

/* ---------------------------------------------------------------- the workspace */
static int list_dir(const char *path, char names[][64], char *dirs, int max)
{
    int n = 0;
    unsigned int position = 0; BarnixDirEntry entry;
    while (n < max && barnix->directory(path, &position, &entry) > 0) {
        if (entry.name[0] == '.' || g_len(entry.name) >= 64) continue;
        g_copy(names[n], entry.name, 64); dirs[n] = entry.kind == 4; n++;
    }
    for (int i = 1; i < n; i++) {   /* folders first, then by name */
        char name[64], dir = dirs[i]; g_copy(name, names[i], 64);
        int j = i;
        while (j > 0) {
            int before = dir != dirs[j - 1] ? dir > dirs[j - 1] : 0;
            if (dir == dirs[j - 1]) { const char *a = name, *b = names[j - 1]; while (*a && (*a | 32) == (*b | 32)) { a++; b++; } before = (*a | 32) < (*b | 32); }
            if (!before) break;
            g_copy(names[j], names[j - 1], 64); dirs[j] = dirs[j - 1]; j--;
        }
        g_copy(names[j], name, 64); dirs[j] = dir;
    }
    return n;
}
static void tree_insert(int at, const char *dir, int depth)
{
    static char names[200][64]; static char dirs[200];
    int n = list_dir(dir, names, dirs, 200);
    if (pool.tree_count + n > TREE_MAX) n = TREE_MAX - pool.tree_count;
    if (n <= 0) return;
    for (int i = pool.tree_count - 1; i >= at; i--) pool.tree[i + n] = pool.tree[i];
    for (int i = 0; i < n; i++) {
        Node *node = &pool.tree[at + i];
        g_path_join(node->path, sizeof(node->path), dir, names[i]);
        node->depth = (short)depth; node->dir = dirs[i]; node->open = 0;
    }
    pool.tree_count += n;
}
static void tree_load(GWindow *w)
{
    pool.tree_count = 0; pool.tree_top = 0; pool.tree_selected = 0;
    tree_insert(0, S->folder, 0);
    pool.files_ready = 0;
}
static void tree_toggle(int i)
{
    Node *node = &pool.tree[i];
    if (!node->dir) return;
    if (node->open) {   /* close: drop the descendants */
        int end = i + 1;
        while (end < pool.tree_count && pool.tree[end].depth > node->depth) end++;
        int removed = end - i - 1;
        for (int k = end; k < pool.tree_count; k++) pool.tree[k - removed] = pool.tree[k];
        pool.tree_count -= removed;
        node->open = 0;
    } else { node->open = 1; tree_insert(i + 1, node->path, node->depth + 1); }
}
/* Every file of the workspace (Quick Open, search in files). */
static void walk(const char *dir, int depth, int prefix)
{
    if (depth > 8) return;
    unsigned int position = 0; BarnixDirEntry entry;
    char path[300];
    while (pool.file_count < FILES_MAX && barnix->directory(dir, &position, &entry) > 0) {
        if (entry.name[0] == '.') continue;
        g_path_join(path, sizeof(path), dir, entry.name);
        if (entry.kind == 4) walk(path, depth + 1, prefix);
        else if (g_len(path + prefix) < 160) g_copy(pool.files[pool.file_count++], path + prefix, 160);
    }
}
static void files_ready(GWindow *w)
{
    if (pool.files_ready) return;
    pool.file_count = 0;
    int prefix = g_len(S->folder);
    if (prefix > 1) prefix++; else prefix = 1;
    walk(S->folder, 0, g_equal(S->folder, "/") ? 1 : prefix);
    pool.files_ready = 1;
}
static void file_path(GWindow *w, int i, char *out, int capacity) { g_path_join(out, capacity, S->folder, pool.files[i]); }
static void open_folder(GWindow *w, const char *folder)
{
    unsigned int position = 0; BarnixDirEntry entry;
    if (barnix->directory(folder, &position, &entry) < 0) { g_error(w, "Barnix Code", "This folder cannot be opened."); return; }
    g_copy(S->folder, folder, sizeof(S->folder));
    int n = g_len(S->folder);
    if (n > 1 && S->folder[n - 1] == '/') S->folder[n - 1] = 0;
    g_copy(S->cwd, S->folder, sizeof(S->cwd));
    tree_load(w);
    save_session(w);
}

/* ---------------------------------------------------------------- session */
static void session_path(char *out, int capacity) { g_path_join(out, capacity, g_home(), ".barnix-code.cfg"); }
static void save_session(GWindow *w)
{
    char path[256], text[4096]; int n = 0;
    session_path(path, sizeof(path));
    n += g_format(text + n, sizeof(text) - n, "folder=%s\nsidebar=%d\npanel=%d\npanel_h=%d\n", S->folder, S->sidebar, S->panel, S->panel_h);
    for (int i = 0; i < DOCS; i++)
        if (pool.docs[i] && pool.docs[i]->used && pool.docs[i]->path[0])
            n += g_format(text + n, sizeof(text) - n, "%s=%s\n", i == S->active ? "active" : "open", pool.docs[i]->path);
    g_write_file(path, text, n);
}
static void load_session(GWindow *w)
{
    char path[256], text[4096];
    session_path(path, sizeof(path));
    int n = g_read_file(path, text, sizeof(text) - 1);
    if (n < 0) return;
    text[n] = 0;
    int active = -1;
    for (char *line = text; *line;) {
        char *end = line; while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        if (line[0] == 'f' && line[6] == '=') { unsigned int position = 0; BarnixDirEntry entry; if (barnix->directory(line + 7, &position, &entry) >= 0) g_copy(S->folder, line + 7, sizeof(S->folder)); }
        else if (line[0] == 's' && line[7] == '=') S->sidebar = line[8] != '0';
        else if (line[0] == 'p' && line[5] == '=') S->panel = line[6] != '0';
        else if (line[0] == 'p' && line[7] == '=') { int h = g_atoi(line + 8); if (h >= 80 && h <= 600) S->panel_h = h; }
        else if ((line[0] == 'o' && line[4] == '=') || (line[0] == 'a' && line[6] == '=')) {
            const char *file = line + (line[0] == 'o' ? 5 : 7);
            if (barnix->size(file) >= 0) { int slot = open_file(w, file); if (line[0] == 'a') active = slot; }
        }
        *end = saved; line = *end ? end + 1 : end;
    }
    if (active >= 0) S->active = active;
}

/* ---------------------------------------------------------------- terminal and output */
static void term_out(const char *text)
{
    do {
        if (pool.term_count == TERM_LINES) { for (int i = 1; i < TERM_LINES; i++) g_copy(pool.term[i - 1], pool.term[i], TERM_WIDTH * 2); pool.term_count--; }
        int n = 0, columns = 0;
        while (text[n] && text[n] != '\n' && n < TERM_WIDTH * 2 - 4) { if (((unsigned char)text[n] & 0xc0) != 0x80 && ++columns > TERM_WIDTH - 1) break; n++; }
        for (int i = 0; i < n; i++) pool.term[pool.term_count][i] = text[i] == '\t' || text[i] == '\r' || text[i] == '\f' ? ' ' : text[i];
        pool.term[pool.term_count][n] = 0;
        pool.term_count++;
        text += n;
        if (*text == '\n') text++;
    } while (*text);
    pool.term_scroll = 0;
}
static void term_lines(const char *text, int n)
{
    static char line[TERM_WIDTH * 2];
    int at = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || text[i] == '\n') { line[at] = 0; if (at || i < n) term_out(line); at = 0; }
        else if (at < (int)sizeof(line) - 1) line[at++] = text[i];
    }
}
static void output_set(const char *text, int n)
{
    if (n > 32767) n = 32767;
    copy_text(pool.output, text, n); pool.output[n] = 0; pool.output_length = n; pool.output_top = 0;
}
/* Bottom panel geometry (needed to run programs inside it). */
static void layout(GWindow *w, int *side_w, int *editor_x, int *editor_y, int *editor_w, int *editor_h, int *panel_y)
{
    int sw = S->sidebar ? 230 : 0, ex = 44 + sw, status = 22, ph = S->panel ? S->panel_h : 0;
    if (ph > w->h - 140) ph = w->h - 140;
    if (ph < 0) ph = 0;
    *side_w = sw; *editor_x = ex; *editor_y = 30;
    *editor_w = w->w - ex; *panel_y = w->h - status - ph;
    *editor_h = *panel_y - 30;
}
/* Runs a program in the bottom panel (its keyboard and screen are there). */
static int run_in_panel(GWindow *w, const char *program, int argc, const char *const *argv, int *status, int show)
{
    S->panel = 1; S->panel_tab = PANEL_TERMINAL;
    int sw, ex, ey, ew, eh, py;
    layout(w, &sw, &ex, &ey, &ew, &eh, &py);
    g_invalidate(w); w->busy = 1; g_flush();
    barnix->cd(S->cwd);
    if (show && barnix->version >= 14) barnix->run_area(w->x + ex, w->y + py + 24, ew, S->panel_h - 24);
    int n = barnix->run(program, argc, argv, pool.scratch, 256 * 1024, pool.swap, status);
    bgx_keys_release();
    w->busy = 0;
    char cwd[256];
    if (barnix->getcwd(cwd, sizeof(cwd)) >= 0) g_copy(S->cwd, cwd, sizeof(S->cwd));
    g_invalidate(w);
    if (n >= 0) pool.scratch[n] = 0;
    return n;
}
static int program_path(GWindow *w, const char *name, char *path, int capacity)
{
    int slash = 0;
    for (const char *p = name; *p; p++) if (*p == '/') slash = 1;
    if (slash) { if (name[0] == '/') g_copy(path, name, capacity); else g_path_join(path, capacity, S->cwd, name); }
    else { g_path_join(path, capacity, "/usr/bin", name); if (barnix->size(path) < 0) g_path_join(path, capacity, "/bin", name); }
    return barnix->size(path) >= 0;
}
static void terminal_command(GWindow *w, char *line)
{
    char prompt[300]; g_format(prompt, sizeof(prompt), "%s$ %s", S->cwd, line);
    term_out(prompt);
    char *argv[16]; int argc = 0;
    for (char *p = line; *p && argc < 15;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (*p == '"') { argv[argc++] = ++p; while (*p && *p != '"') p++; if (*p) *p++ = 0; continue; }
        argv[argc++] = p; while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    char path[300];
    if (g_equal(argv[0], "clear") || g_equal(argv[0], "cls")) { pool.term_count = 0; return; }
    if (g_equal(argv[0], "cd")) {
        char target[300];
        if (argc < 2) g_copy(target, S->folder, sizeof(target));
        else if (argv[1][0] == '/') g_copy(target, argv[1], sizeof(target));
        else g_path_join(target, sizeof(target), S->cwd, argv[1]);
        unsigned int position = 0; BarnixDirEntry entry;
        if (barnix->directory(target, &position, &entry) < 0) { term_out(g_tr("No such folder.")); return; }
        barnix->cd(target);
        if (barnix->getcwd(S->cwd, sizeof(S->cwd)) < 0) g_copy(S->cwd, target, sizeof(S->cwd));
        return;
    }
    if (g_equal(argv[0], "pwd")) { term_out(S->cwd); return; }
    if (g_equal(argv[0], "code") && argc > 1) {   /* open a file in the editor, as VS Code's command */
        if (argv[1][0] == '/') g_copy(path, argv[1], sizeof(path)); else g_path_join(path, sizeof(path), S->cwd, argv[1]);
        open_file(w, path);
        return;
    }
    if (!program_path(w, argv[0], path, sizeof(path))) { char text[200]; g_format(text, sizeof(text), g_tr("Unknown command: %s"), argv[0]); term_out(text); return; }
    int status = 0;
    int n = run_in_panel(w, path, argc, (const char *const *)argv, &status, 1);
    if (n < 0) { term_out(g_tr("The program cannot be run.")); return; }
    term_lines(pool.scratch, n);
    if (status) { char text[64]; g_format(text, sizeof(text), g_tr("(exit status %d)"), status); term_out(text); }
}

/* ---------------------------------------------------------------- problems */
static void parse_problems(const char *text, int n)
{
    pool.problem_count = 0;
    for (int i = 0; i < n && pool.problem_count < PROBLEMS_MAX;) {
        int end = i; while (end < n && text[end] != '\n') end++;
        char line[400]; int k = 0;
        for (int j = i; j < end && k < 399; j++) line[k++] = text[j];
        line[k] = 0;
        i = end + 1;
        Problem *p = &pool.problems[pool.problem_count];
        /* GCC: file:line:col: error|warning: message */
        char *colon = 0;
        for (char *q = line; *q; q++) if (*q == ':' && q[1] >= '0' && q[1] <= '9') { colon = q; break; }
        int is_error = 0, is_warning = 0;
        for (char *q = line; *q; q++) {
            if (q[0] == 'e' && q[1] == 'r' && q[2] == 'r' && q[3] == 'o' && q[4] == 'r') is_error = 1;
            if (q[0] == 'w' && q[1] == 'a' && q[2] == 'r' && q[3] == 'n') is_warning = 1;
        }
        if (colon && (is_error || is_warning)) {
            *colon = 0;
            g_copy(p->file, line, sizeof(p->file));
            p->line = g_atoi(colon + 1);
            char *rest = colon + 1; while (*rest >= '0' && *rest <= '9') rest++;
            p->column = 0;
            if (*rest == ':' && rest[1] >= '0' && rest[1] <= '9') { p->column = g_atoi(rest + 1); rest++; while (*rest >= '0' && *rest <= '9') rest++; }
            while (*rest == ':' || *rest == ' ') rest++;
            p->error = is_error;
            g_copy(p->message, rest, sizeof(p->message));
            if (p->line > 0) pool.problem_count++;
            continue;
        }
        /* Python: File "x.py", line 12  (the message is on a later line) */
        char *file = 0;
        for (char *q = line; *q; q++) if (q[0] == 'F' && q[1] == 'i' && q[2] == 'l' && q[3] == 'e' && q[4] == ' ' && q[5] == '"') { file = q + 6; break; }
        if (file) {
            char *quote = file; while (*quote && *quote != '"') quote++;
            if (*quote && quote[1] == ',' && quote[2] == ' ' && quote[3] == 'l') {
                *quote = 0; g_copy(p->file, file, sizeof(p->file));
                p->line = g_atoi(quote + 8); p->column = 0; p->error = 1;
                g_copy(p->message, "", sizeof(p->message));
                if (p->line > 0 && p->file[0] != '<') pool.problem_count++;
            }
        } else if (pool.problem_count && !pool.problems[pool.problem_count - 1].message[0] && line[0] && line[0] != ' ')
            g_copy(pool.problems[pool.problem_count - 1].message, line, sizeof(pool.problems[0].message));
    }
    /* Python names the error last: give it to the innermost frame */
    for (int i = 0; i < pool.problem_count; i++) if (!pool.problems[i].message[0]) g_copy(pool.problems[i].message, "Python traceback", sizeof(pool.problems[0].message));
}
static void go_to(GWindow *w, const char *file, int line, int column)
{
    char path[300];
    if (file[0] == '/') g_copy(path, file, sizeof(path)); else g_path_join(path, sizeof(path), S->cwd, file);
    if (open_file(w, path) < 0) return;
    Doc *d = active_doc(w);
    gte_goto_line(&d->edit, line - 1);
    int pos = d->edit.cursor;
    while (column > 1 && pos < d->edit.length && d->text[pos] != '\n') { pos = g_utf8_next(d->text, d->edit.length, pos); column--; }
    d->edit.cursor = pos; d->edit.shown = -1;
    S->focus = F_EDITOR;
}

/* ---------------------------------------------------------------- running */
static void run_active(GWindow *w)
{
    Doc *d = active_doc(w);
    if (!d) return;
    if (!d->path[0]) { ask_save_as(w, d); return; }
    if (d->edit.modified && save_doc(w, d)) return;
    char path[300], out[300];
    g_copy(path, d->path, sizeof(path));
    g_copy(out, path, sizeof(out));
    for (int i = g_len(out) - 1; i >= 0 && out[i] != '/'; i--) if (out[i] == '.') { out[i] = 0; break; }
    int status = 0, n;
    pool.problem_count = 0;
    if (d->lang == LANG_C || d->lang == LANG_CPP) {
        const char *compiler = d->lang == LANG_CPP ? "/usr/bin/g++" : "/usr/bin/gcc";
        if (barnix->size(compiler) < 0) { set_status(w, "GCC is not installed: BarnStore, package gcc."); g_open(g_app_named("BarnStore"), "gcc"); return; }
        set_status(w, "Building with GCC...");
        const char *argv[] = {compiler, "-Wall", "-O2", "-fdiagnostics-plain-output", path, "-o", out};
        n = run_in_panel(w, compiler, 7, argv, &status, 0);
        if (n < 0) { set_status(w, "GCC cannot be run."); return; }
        output_set(pool.scratch, n);
        parse_problems(pool.scratch, n);
        if (status) { S->panel_tab = PANEL_PROBLEMS; set_status(w, "The build failed: see Problems."); g_sound(G_SOUND_ERROR); return; }
        char line[400]; g_format(line, sizeof(line), "%s$ %s", S->cwd, out); term_out(line);
        const char *run_argv[] = {out};
        n = run_in_panel(w, out, 1, run_argv, &status, 1);
    } else if (d->lang == LANG_PY) {
        if (barnix->size("/usr/bin/python3") < 0) { set_status(w, "Python is not installed: BarnStore, package python."); g_open(g_app_named("BarnStore"), "python"); return; }
        char line[400]; g_format(line, sizeof(line), "%s$ python3 %s", S->cwd, path); term_out(line);
        const char *argv[] = {"python3", path};
        n = run_in_panel(w, "/usr/bin/python3", 2, argv, &status, 1);
        if (n >= 0) { parse_problems(pool.scratch, n); output_set(pool.scratch, n); }
    } else if (d->lang == LANG_NOVA) {
        g_append(out, ".jar", sizeof(out));
        if (barnix->size(out) < 0) { set_status(w, "Build the Nova program with JDK 21 first: nova build main.nova -o main.jar"); return; }
        const char *argv[] = {"java", "-jar", out};
        n = run_in_panel(w, "/usr/bin/java", 3, argv, &status, 1);
    } else if (d->lang == LANG_HTML || d->lang == LANG_MD || d->lang == LANG_CSS || d->lang == LANG_JS) {
        char url[300]; g_format(url, sizeof(url), "file://%s", path);
        g_browse(url);
        set_status(w, "Opened in Gouse Web.");
        return;
    } else { set_status(w, "This kind of file is not run: open a terminal (Ctrl+`)."); return; }
    if (n < 0) { set_status(w, "The program cannot be run."); return; }
    term_lines(pool.scratch, n);
    char text[80]; g_format(text, sizeof(text), g_tr("The program ended with status %d."), status);
    g_copy(S->status, text, sizeof(S->status));
    if (status && pool.problem_count) S->panel_tab = PANEL_PROBLEMS;
}

/* ---------------------------------------------------------------- completion (IntelliSense) */
static const Sym *symbols_of(int lang)
{
    return lang == LANG_PY ? sym_py : lang == LANG_C || lang == LANG_CPP ? sym_c : lang == LANG_JS ? sym_js :
           lang == LANG_HTML ? sym_html : lang == LANG_CSS ? sym_css : 0;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* 3 the same beginning, 2 the same ignoring case, 1 the letters in order, 0 no */
static int match_score(const char *name, const char *prefix, int n)
{
    int k = 0;
    while (k < n && name[k] == prefix[k]) k++;
    if (k == n) return 3;
    k = 0;
    while (k < n && name[k] && lower(name[k]) == lower(prefix[k])) k++;
    if (k == n) return 2;
    int q = 0;
    for (const char *t = name; *t && q < n; t++) if (lower(*t) == lower(prefix[q])) q++;
    return q == n && n >= 2 && lower(name[0]) == lower(prefix[0]) ? 1 : 0;
}
static void comp_add(const char *name, int name_length, const char *detail, int kind, int score)
{
    if (pool.comp_count == 60 || name_length <= 0 || name_length >= 64) return;
    for (int i = 0; i < pool.comp_count; i++) {
        if ((int)g_len(pool.comp[i].name) == name_length) {
            int same = 1; for (int k = 0; k < name_length; k++) if (pool.comp[i].name[k] != name[k]) same = 0;
            if (same) { if (kind != K_VARIABLE && pool.comp[i].kind == K_VARIABLE) { pool.comp[i].kind = (unsigned char)kind; pool.comp[i].detail = detail; } return; }
        }
    }
    int at = pool.comp_count++;
    for (int k = 0; k < name_length; k++) pool.comp[at].name[k] = name[k];
    pool.comp[at].name[name_length] = 0;
    pool.comp[at].detail = detail; pool.comp[at].kind = (unsigned char)kind; pool.comp[at].score = score;
}
static void complete_close(void) { pool.comp_open = 0; pool.comp_count = 0; }
/* force: Ctrl+Space (every name; a short beginning is enough) */
static void complete_update(Doc *d, int force)
{
    int cursor = d->edit.cursor, start = cursor;
    while (start > 0 && (is_word(d->text[start - 1]) || (d->lang == LANG_CSS && d->text[start - 1] == '-'))) start--;
    int n = cursor - start;
    char prefix[64];
    if (n >= 63) { complete_close(); return; }
    copy_text(prefix, d->text + start, n); prefix[n] = 0;
    /* "receiver." (or "std::"): members */
    char receiver[64] = ""; int member = 0;
    if (start > 0 && d->text[start - 1] == '.') member = 1;
    else if (start > 1 && d->text[start - 1] == ':' && d->text[start - 2] == ':') member = 2;
    if (member) {
        int end = start - member, r = end;
        while (r > 0 && is_word(d->text[r - 1])) r--;
        if (end - r <= 0 || end - r >= 63) member = 0;
        else { copy_text(receiver, d->text + r, end - r); receiver[end - r] = 0; }
    }
    if (!force && !member && n < 2) { complete_close(); return; }
    if (n && d->text[start] >= '0' && d->text[start] <= '9') { complete_close(); return; }
    /* inside a comment or a string: nothing (the colors know) */
    pool.comp_count = 0;
    const Sym *table = symbols_of(d->lang);
    char separator[3] = {member == 2 ? ':' : '.', member == 2 ? ':' : 0, 0};
    int rl = g_len(receiver), sl = g_len(separator);
    int known_receiver = 0;
    if (member && table)
        for (int i = 0; table[i].name; i++) {
            const char *name = table[i].name;
            int k = 0; while (k < rl && name[k] == receiver[k]) k++;
            if (k == rl && name[rl] == separator[0] && (sl == 1 || name[rl + 1] == ':')) { known_receiver = 1; break; }
        }
    for (int pass = 0; table && pass < 1; pass++)
        for (int i = 0; table[i].name; i++) {
            const char *name = table[i].name, *dot = 0;
            for (const char *t = name; *t; t++) if (*t == '.' || (t[0] == ':' && t[1] == ':')) { dot = t; break; }
            const char *shown = name;
            if (member) {
                if (!dot) continue;
                int own = 1;
                if (name[0] == '*') own = !known_receiver;
                else { int k = 0; while (k < rl && name[k] == receiver[k]) k++; own = k == rl && name + rl == dot; }
                if (!own) continue;
                shown = dot + (dot[0] == '.' ? 1 : 2);
            } else if (dot || name[0] == '*') continue;
            int score = match_score(shown, prefix, n);
            if (!score && n) continue;
            if (table[i].kind == K_SNIPPET && !(n && score >= 2)) continue;
            comp_add(shown, g_len(shown), table[i].detail, table[i].kind, score * 4 + (table[i].kind == K_SNIPPET ? 3 : 1));
        }
    if (!member || !known_receiver) {
        /* the language's words */
        const Lang *L = &langs[d->lang];
        for (int list = 0; list < 2 && !member; list++)
            for (const char *const *k = list ? L->types : L->keywords; *k; k++) {
                int score = match_score(*k, prefix, n);
                if (score && (int)g_len(*k) > n) comp_add(*k, g_len(*k), "", list ? K_CLASS : K_KEYWORD, score * 4);
            }
        /* the words of this file */
        int words = 0;
        for (int p = 0; p < d->edit.length && words < 400;) {
            if (!is_word(d->text[p]) || (p > 0 && is_word(d->text[p - 1]))) { p++; continue; }
            int e = p; while (e < d->edit.length && is_word(d->text[e])) e++;
            int length = e - p;
            if (length >= 3 && length < 64 && p != start && !(d->text[p] >= '0' && d->text[p] <= '9')) {
                char word[64]; copy_text(word, d->text + p, length); word[length] = 0;
                int score = match_score(word, prefix, n);
                if (score && length > n) { comp_add(word, length, "", K_VARIABLE, score * 4 - 1); words++; }
            }
            p = e;
        }
    }
    /* best first */
    for (int i = 1; i < pool.comp_count; i++) {
        int j = i;
        while (j > 0) {
            int a = pool.comp[j].score, b = pool.comp[j - 1].score;
            if (a < b || (a == b && g_len(pool.comp[j].name) >= g_len(pool.comp[j - 1].name))) break;
            char saved[sizeof(pool.comp[0])];
            copy_text(saved, (char *)&pool.comp[j], sizeof(saved));
            copy_text((char *)&pool.comp[j], (char *)&pool.comp[j - 1], sizeof(saved));
            copy_text((char *)&pool.comp[j - 1], saved, sizeof(saved));
            j--;
        }
    }
    pool.comp_start = start;
    pool.comp_selected = 0; pool.comp_top = 0;
    pool.comp_open = pool.comp_count > 0 && !(pool.comp_count == 1 && g_equal(pool.comp[0].name, prefix));
}
static int line_start_of(const Doc *d, int pos);
static void complete_accept(Doc *d)
{
    if (!pool.comp_open || pool.comp_selected >= pool.comp_count) return;
    snapshot(d, 1);
    const char *name = pool.comp[pool.comp_selected].name;
    int kind = pool.comp[pool.comp_selected].kind;
    int from = pool.comp_start, to = d->edit.cursor;
    while (to < d->edit.length && is_word(d->text[to])) to++;   /* the rest of the word goes too */
    static char text[1024]; int n = 0, cursor = -1;
    if (kind == K_SNIPPET) {   /* the body, indented as this line; $0 is the cursor */
        const char *body = pool.comp[pool.comp_selected].detail;
        int line = line_start_of(d, from), indent = 0;
        while (d->text[line + indent] == ' ') indent++;
        for (const char *b = body; *b && n < 1000; b++) {
            if (b[0] == '$' && b[1] == '0') { cursor = n; b++; continue; }
            text[n++] = *b;
            if (*b == '\n') for (int k = 0; k < indent && n < 1000; k++) text[n++] = ' ';
        }
    } else {
        n = g_len(name); copy_text(text, name, n);
        int call = kind == K_FUNCTION && (to >= d->edit.length || d->text[to] != '(') && d->lang != LANG_HTML && d->lang != LANG_CSS;
        if (call) { text[n++] = '('; text[n++] = ')'; cursor = n - 1; }
        if (d->lang == LANG_CSS && kind == K_PROPERTY) { text[n++] = ':'; text[n++] = ' '; }
    }
    d->edit.anchor = from; d->edit.cursor = to;
    gte_insert(&d->edit, text, n);
    if (cursor >= 0) d->edit.cursor = from + cursor;
    d->edit.anchor = -1; d->edit.modified = 1;
    complete_close();
}
/* The signature of the call the cursor is in (parameter hints). */
static const char *signature_at(const Doc *d, int *open_at)
{
    const Sym *table = symbols_of(d->lang);
    if (!table) return 0;
    int depth = 0, p = d->edit.cursor - 1, stop = d->edit.cursor - 400;
    for (; p >= 0 && p > stop && d->text[p] != '\n'; p--) {
        if (d->text[p] == ')') depth++;
        else if (d->text[p] == '(') { if (!depth) break; depth--; }
    }
    if (p < 0 || p <= stop || d->text[p] != '(') return 0;
    int e = p, b = e;
    while (b > 0 && is_word(d->text[b - 1])) b--;
    int length = e - b;
    if (length <= 0 || length >= 63) return 0;
    char word[64]; copy_text(word, d->text + b, length); word[length & 63] = 0;
    char receiver[64] = "";
    if (b > 0 && d->text[b - 1] == '.') { int r = b - 1; while (r > 0 && is_word(d->text[r - 1])) r--; if (b - 1 - r < 63) { copy_text(receiver, d->text + r, b - 1 - r); receiver[b - 1 - r] = 0; } }
    for (int i = 0; table[i].name; i++) {
        if (table[i].kind != K_FUNCTION && table[i].kind != K_CLASS) continue;
        const char *name = table[i].name, *dot = 0;
        for (const char *t = name; *t; t++) if (*t == '.' || (t[0] == ':' && t[1] == ':')) dot = t;
        const char *last = dot ? dot + (dot[0] == '.' ? 1 : 2) : name;
        if (!g_equal(last, word)) continue;
        if (dot && name[0] != '*') { int rl = g_len(receiver), k = 0; while (k < rl && name[k] == receiver[k]) k++; if (k != rl || name + rl != dot) continue; }
        if (!dot && receiver[0]) continue;
        *open_at = p;
        return table[i].detail;
    }
    return 0;
}
static void draw_completion(GWindow *w, BgxCanvas *c, Doc *d, int ex, int ey, int ew, int eh)
{
    (void)w;
    int line = gte_line_of(&d->edit, d->edit.cursor), start = gte_line_start(&d->edit, line), column = 0;
    for (int p = start; p < d->edit.cursor; p = g_utf8_next(d->text, d->edit.length, p)) column++;
    int cx = ex + gte_gutter_width(&d->edit) + 4 + (column - d->edit.left) * 8;
    int cy = ey + 2 + (line - d->edit.top) * 16;
    if (cy < ey || cy >= ey + eh) return;
    /* parameter hints above the line */
    int open_at;
    const char *signature = signature_at(d, &open_at);
    if (signature && !pool.comp_open) {
        int width = g_len(signature) * 8 + 16, x = cx - 12;
        if (x + width > ex + ew) x = ex + ew - width;
        if (x < ex) x = ex;
        int y = cy - 24 < ey ? cy + 18 : cy - 24;
        bgx_fill(c, x, y, width, 22, 0x252526); bgx_frame(c, x, y, width, 22, 1, 0x454545);
        g_text_fit_raw(c, x + 8, y + 3, width - 12, signature, 0xd4d4d4);
    }
    if (!pool.comp_open) return;
    int rows = pool.comp_count < 9 ? pool.comp_count : 9, width = 380, height = rows * 19 + 4;
    int x = cx - 30, y = cy + 18;
    if (x + width > ex + ew) x = ex + ew - width;
    if (x < ex) x = ex;
    if (y + height + 24 > ey + eh) y = cy - height - 2;
    if (pool.comp_selected < pool.comp_top) pool.comp_top = pool.comp_selected;
    if (pool.comp_selected >= pool.comp_top + rows) pool.comp_top = pool.comp_selected - rows + 1;
    bgx_fill(c, x, y, width, height, 0x252526); bgx_frame(c, x, y, width, height, 1, 0x454545);
    static const char letters[] = "kfvsmcp";
    static const unsigned int colors[] = {0x569cd6, 0xb180d7, 0x75beff, 0xc5c5c5, 0xd7ba7d, 0xee9d28, 0x75beff};
    for (int r = 0; r < rows; r++) {
        int i = pool.comp_top + r, ry = y + 2 + r * 19;
        if (i == pool.comp_selected) bgx_fill(c, x + 1, ry, width - 2, 19, 0x04395e);
        int kind = pool.comp[i].kind;
        char letter[2] = {letters[kind], 0};
        bgx_frame(c, x + 6, ry + 3, 13, 13, 1, colors[kind]);
        g_text_raw(c, x + 9, ry + 1, letter, colors[kind]);
        int n = d->edit.cursor - pool.comp_start;
        g_text_raw(c, x + 26, ry + 1, pool.comp[i].name, 0xd4d4d4);
        if (n > 0 && n <= (int)g_len(pool.comp[i].name)) {   /* the typed part in blue */
            char typed[64]; copy_text(typed, pool.comp[i].name, n); typed[n] = 0;
            if (match_score(pool.comp[i].name, d->text + pool.comp_start, n) >= 2) g_text_raw(c, x + 26, ry + 1, typed, 0x2aaaff);
        }
        if (i == pool.comp_selected && pool.comp[i].detail && pool.comp[i].detail[0]) {
            char detail[80]; int k = 0;
            const char *from = pool.comp[i].detail;
            int nl = g_len(pool.comp[i].name), same = 1;
            for (int q = 0; q < nl; q++) if (from[q] != pool.comp[i].name[q]) { same = 0; break; }
            if (same && from[nl] == '(') from += nl;   /* "sqrt(x) -> float": the name is already there */
            for (const char *t = from; *t && k < 79; t++) detail[k++] = *t == '\n' ? ' ' : *t;
            detail[k] = 0;
            int dx = x + 34 + g_len(pool.comp[i].name) * 8;
            g_text_fit_raw(c, dx, ry + 1, x + width - dx - 6, detail, 0x9d9d9d);
        }
    }
    if (pool.comp[pool.comp_selected].detail && g_len(pool.comp[pool.comp_selected].detail) > 30) {   /* the whole detail below */
        char detail[200]; int k = 0;
        for (const char *t = pool.comp[pool.comp_selected].detail; *t && k < 199; t++) detail[k++] = *t == '\n' ? ' ' : *t;
        detail[k] = 0;
        bgx_fill(c, x, y + height, width, 22, 0x2d2d30); bgx_frame(c, x, y + height, width, 22, 1, 0x454545);
        g_text_fit_raw(c, x + 8, y + height + 3, width - 14, detail, 0xd4d4d4);
    }
}

/* ---------------------------------------------------------------- breakpoints and the debugger */
static void toggle_break(Doc *d, int line)
{
    for (int i = 0; i < d->break_count; i++)
        if (d->breaks[i] == line) { d->breaks[i] = d->breaks[--d->break_count]; d->edit.dot_count = d->break_count; return; }
    if (d->break_count < 64) d->breaks[d->break_count++] = line;
    d->edit.dot_count = d->break_count;
}
static int run_in_panel(GWindow *w, const char *program, int argc, const char *const *argv, int *status, int show);
static void debug_active(GWindow *w)
{
    Doc *d = active_doc(w);
    if (!d) return;
    if (d->lang != LANG_PY) { set_status(w, "The debugger is for Python files; F5 runs this one, Ctrl+F5 too."); return; }
    if (!d->path[0]) { ask_save_as(w, d); return; }
    if (d->edit.modified && save_doc(w, d)) return;
    if (barnix->size("/usr/bin/python3") < 0) { set_status(w, "Python is not installed: BarnStore, package python."); g_open(g_app_named("BarnStore"), "python"); return; }
    const char *helper = "/usr/share/gouse/debug.py";
    if (barnix->size(helper) < 0) { set_status(w, "The debugger helper is missing (/usr/share/gouse/debug.py)."); return; }
    char breaks[300] = "", number[12];
    for (int i = 0; i < d->break_count; i++) { g_format(number, sizeof(number), i ? ",%d" : "%d", d->breaks[i]); g_append(breaks, number, sizeof(breaks)); }
    if (!breaks[0]) g_copy(breaks, "0", sizeof(breaks));
    const char *state = "/tmp/.barnix-code-debug";
    barnix->rm(state);
    const char *argv[] = {"python3", helper, "--breaks", breaks, "--state", state, d->path};
    /* the whole area right of the side bar: the program's console needs 80x25 */
    int sw, ex, ey, ew, eh, py;
    layout(w, &sw, &ex, &ey, &ew, &eh, &py);
    g_invalidate(w); w->busy = 1; g_flush();
    barnix->cd(S->cwd);
    if (barnix->version >= 14) barnix->run_area(w->x + ex, w->y + 30, ew, w->h - 22 - 30);
    int status = 0;
    int n = barnix->run("/usr/bin/python3", 7, argv, pool.scratch, 256 * 1024, pool.swap, &status);
    bgx_keys_release();
    w->busy = 0;
    g_invalidate(w);
    if (n < 0) { set_status(w, "Python cannot be run."); return; }
    char text[400];
    int got = g_read_file(state, text, sizeof(text) - 1);
    if (got > 7) {   /* breakpoints as the debugger left them */
        text[got] = 0;
        d->break_count = 0;
        for (char *p = text + 7; *p && d->break_count < 64;) {
            int line = g_atoi(p);
            if (line > 0) d->breaks[d->break_count++] = line;
            while (*p && *p != ',') p++;
            if (*p) p++;
        }
        d->edit.dot_count = d->break_count;
    }
    set_status(w, "Debugging finished.");
}

/* ---------------------------------------------------------------- text helpers */
static int line_start_of(const Doc *d, int pos) { while (pos > 0 && d->text[pos - 1] != '\n') pos--; return pos; }
static int line_end_of(const Doc *d, int pos) { while (pos < d->edit.length && d->text[pos] != '\n') pos++; return pos; }
static void replace_range(Doc *d, int from, int to, const char *text, int n)
{
    d->edit.anchor = from; d->edit.cursor = to;
    if (from == to) d->edit.anchor = -1;
    gte_insert(&d->edit, text, n);
    if (!n && from != to) { /* gte_insert with nothing removes the selection only when there is one */ }
    d->edit.modified = 1;
}
static void selected_lines(const Doc *d, int *first, int *last)
{
    int from = d->edit.cursor, to = d->edit.cursor;
    gte_selection(&d->edit, &from, &to);
    if (to > from && d->text[to - 1] == '\n') to--;
    *first = line_start_of(d, from);
    *last = line_end_of(d, to);
}
static void move_lines(Doc *d, int down)
{
    int first, last;
    selected_lines(d, &first, &last);
    static char block[TEXT_MAX];
    if (down) {
        if (last >= d->edit.length) return;
        int next_end = line_end_of(d, last + 1);
        int n = 0, a = last + 1 - first, b = next_end - (last + 1);
        copy_text(block, d->text + last + 1, b); n = b; block[n++] = '\n';
        copy_text(block + n, d->text + first, a - 1); n += a - 1;
        int offset = d->edit.cursor - first;
        replace_range(d, first, next_end, block, n);
        d->edit.cursor = first + b + 1 + offset; d->edit.anchor = -1;
    } else {
        if (!first) return;
        int prev = line_start_of(d, first - 1);
        int a = last - first, b = first - 1 - prev, n = 0;
        copy_text(block, d->text + first, a); n = a; block[n++] = '\n';
        copy_text(block + n, d->text + prev, b); n += b;
        int offset = d->edit.cursor - first;
        replace_range(d, prev, last, block, n);
        d->edit.cursor = prev + offset; d->edit.anchor = -1;
    }
}
static void duplicate_lines(Doc *d)
{
    int first, last; selected_lines(d, &first, &last);
    static char block[TEXT_MAX];
    int n = 0; block[n++] = '\n'; copy_text(block + n, d->text + first, last - first); n += last - first;
    int offset = d->edit.cursor - first;
    replace_range(d, last, last, block, n);
    d->edit.cursor = last + 1 + offset; d->edit.anchor = -1;
}
static void delete_lines(Doc *d)
{
    int first, last; selected_lines(d, &first, &last);
    if (last < d->edit.length) last++; else if (first > 0) first--;
    replace_range(d, first, last, "", 0);
    d->edit.cursor = first; d->edit.anchor = -1;
}
static void toggle_comment(Doc *d)
{
    const Lang *L = &langs[d->lang];
    const char *open = L->line_comment ? L->line_comment : L->block_open, *close = L->line_comment ? 0 : L->block_close;
    if (!open) return;
    int first, last; selected_lines(d, &first, &last);
    int on = 1, indent = 1000;   /* all commented already? */
    for (int p = first; p <= last;) {
        int e = line_end_of(d, p), q = p; while (q < e && d->text[q] == ' ') q++;
        if (q < e) { if (q - p < indent) indent = q - p; if (!starts(d->text, d->edit.length, q, open)) on = 0; }
        p = e + 1;
    }
    if (indent == 1000) return;
    static char block[TEXT_MAX];
    int n = 0, ol = g_len(open), cl = close ? g_len(close) : 0;
    for (int p = first; p <= last;) {
        int e = line_end_of(d, p), q = p; while (q < e && d->text[q] == ' ') q++;
        if (q >= e) { copy_text(block + n, d->text + p, e - p); n += e - p; }
        else if (on) {
            copy_text(block + n, d->text + p, q - p); n += q - p;
            int s = q + ol; if (s < e && d->text[s] == ' ') s++;
            int t = e;
            if (close && t - cl >= s && starts(d->text, d->edit.length, t - cl, close)) { t -= cl; if (t > s && d->text[t - 1] == ' ') t--; }
            copy_text(block + n, d->text + s, t - s); n += t - s;
        } else {
            copy_text(block + n, d->text + p, indent); n += indent;
            copy_text(block + n, open, ol); n += ol; block[n++] = ' ';
            copy_text(block + n, d->text + p + indent, e - p - indent); n += e - p - indent;
            if (close) { block[n++] = ' '; copy_text(block + n, close, cl); n += cl; }
        }
        if (e >= last) break;
        block[n++] = '\n';
        p = e + 1;
    }
    replace_range(d, first, last, block, n);
    d->edit.anchor = first; d->edit.cursor = first + n;
}
static void indent_lines(Doc *d, int out)
{
    int first, last; selected_lines(d, &first, &last);
    static char block[TEXT_MAX];
    int n = 0;
    for (int p = first; p <= last;) {
        int e = line_end_of(d, p);
        if (out) { int k = 0; while (k < 4 && p + k < e && d->text[p + k] == ' ') k++; copy_text(block + n, d->text + p + k, e - p - k); n += e - p - k; }
        else { if (e > p) { copy_text(block + n, "    ", 4); n += 4; } copy_text(block + n, d->text + p, e - p); n += e - p; }
        if (e >= last) break;
        block[n++] = '\n'; p = e + 1;
    }
    replace_range(d, first, last, block, n);
    d->edit.anchor = first; d->edit.cursor = first + n;
}
static void new_line(Doc *d)
{
    int start = line_start_of(d, d->edit.cursor), p = start;
    char text[200]; int n = 0; text[n++] = '\n';
    while (p < d->edit.cursor && d->text[p] == ' ' && n < 150) { text[n++] = ' '; p++; }
    int indent = n - 1, last = d->edit.cursor - 1;
    while (last >= start && d->text[last] == ' ') last--;
    char before = last >= start ? d->text[last] : 0, after = d->edit.cursor < d->edit.length ? d->text[d->edit.cursor] : 0;
    int opens = before == '{' || before == '[' || before == '(' || ((langs[d->lang].flags & L_COLON) && before == ':');
    if (opens) for (int k = 0; k < 4; k++) text[n++] = ' ';
    if (opens && ((before == '{' && after == '}') || (before == '[' && after == ']') || (before == '(' && after == ')'))) {
        int cursor_at = n;
        text[n++] = '\n'; for (int k = 0; k < indent; k++) text[n++] = ' ';
        int from = d->edit.cursor;
        gte_insert(&d->edit, text, n);
        d->edit.cursor = from + cursor_at;
        return;
    }
    gte_insert(&d->edit, text, n);
}
static int matching(const Doc *d, int pos)
{
    static const char pairs[] = "()[]{}";
    if (pos < 0 || pos >= d->edit.length) return -1;
    char c = d->text[pos];
    for (int k = 0; k < 6; k++) if (pairs[k] == c) {
        int forward = !(k & 1), depth = 0;
        char open = pairs[k & ~1], close = pairs[k | 1];
        for (int p = pos, steps = 0; p >= 0 && p < d->edit.length && steps < 30000; p += forward ? 1 : -1, steps++) {
            if (d->text[p] == open) depth += forward ? 1 : -1;
            else if (d->text[p] == close) depth += forward ? -1 : 1;
            if (!depth) return p;
        }
        return -1;
    }
    return -1;
}
static void update_marks(Doc *d)
{
    d->edit.mark[0] = d->edit.mark[1] = -1;
    int pos = d->edit.cursor, m = matching(d, pos);
    if (m < 0 && pos > 0) { pos--; m = matching(d, pos); }
    if (m >= 0) { d->edit.mark[0] = pos; d->edit.mark[1] = m; }
}
static void select_word(Doc *d)
{
    int a = d->edit.cursor, b = a;
    while (a > 0 && is_word(d->text[a - 1])) a--;
    while (b < d->edit.length && is_word(d->text[b])) b++;
    if (b > a) { d->edit.anchor = a; d->edit.cursor = b; }
}
static int find_text(const Doc *d, const char *needle, int from, int backwards, int match_case)
{
    int n = g_len(needle);
    if (!n) return -1;
    for (int round = 0; round < 2; round++) {
        if (!backwards) {
            for (int i = round ? 0 : from; i + n <= d->edit.length; i++) {
                int k = 0;
                while (k < n && (match_case ? d->text[i + k] == needle[k] : (d->text[i + k] | (d->text[i + k] >= 'A' && d->text[i + k] <= 'Z' ? 32 : 0)) == (needle[k] | (needle[k] >= 'A' && needle[k] <= 'Z' ? 32 : 0)))) k++;
                if (k == n) return i;
            }
        } else {
            for (int i = round ? d->edit.length - n : from; i >= 0; i--) {
                int k = 0;
                while (k < n && (match_case ? d->text[i + k] == needle[k] : (d->text[i + k] | (d->text[i + k] >= 'A' && d->text[i + k] <= 'Z' ? 32 : 0)) == (needle[k] | (needle[k] >= 'A' && needle[k] <= 'Z' ? 32 : 0)))) k++;
                if (k == n) return i;
            }
        }
    }
    return -1;
}
static int count_matches(const Doc *d, const char *needle, int match_case)
{
    int count = 0, n = g_len(needle), at = 0;
    if (!n) return 0;
    for (;;) { int i = find_text(d, needle, at, 0, match_case); if (i < at) break; count++; at = i + n; if (count > 9999) break; }
    return count;
}
static void find_next(GWindow *w, int backwards)
{
    Doc *d = active_doc(w);
    if (!d || !S->find.text[0]) return;
    int from = 0, to = 0;
    if (!gte_selection(&d->edit, &from, &to)) from = to = d->edit.cursor;
    int at = find_text(d, S->find.text, backwards ? from - 1 : to, backwards, S->match_case);
    if (at < 0) { set_status(w, "No results"); return; }
    d->edit.anchor = at; d->edit.cursor = at + g_len(S->find.text); d->edit.shown = -1;
}
static void select_next_occurrence(Doc *d)
{
    int from, to;
    if (!gte_selection(&d->edit, &from, &to)) { select_word(d); return; }
    static char needle[256]; int n = to - from < 255 ? to - from : 255;
    copy_text(needle, d->text + from, n); needle[n] = 0;
    int at = find_text(d, needle, to, 0, 1);
    if (at >= 0) { d->edit.anchor = at; d->edit.cursor = at + n; d->edit.shown = -1; }
}
static void replace_one(GWindow *w)
{
    Doc *d = active_doc(w);
    if (!d || !S->find.text[0]) return;
    int from, to;
    if (gte_selection(&d->edit, &from, &to) && find_text(d, S->find.text, from, 0, S->match_case) == from && to - from == g_len(S->find.text)) {
        snapshot(d, 1);
        replace_range(d, from, to, S->replace.text, g_len(S->replace.text));
    }
    find_next(w, 0);
}
static void replace_all(GWindow *w)
{
    Doc *d = active_doc(w);
    if (!d || !S->find.text[0]) return;
    snapshot(d, 1);
    int n = g_len(S->find.text), r = g_len(S->replace.text), at = 0, count = 0;
    for (;;) {
        int i = find_text(d, S->find.text, at, 0, S->match_case);
        if (i < at) break;
        replace_range(d, i, i + n, S->replace.text, r);
        at = i + r; count++;
        if (count > 50000) break;
    }
    char text[80]; g_format(text, sizeof(text), g_tr("Replaced %d occurrence(s)"), count); g_copy(S->status, text, sizeof(S->status));
}

/* ---------------------------------------------------------------- search in files */
static int text_file(const char *name)
{
    int l = lang_of(name);
    return l != LANG_TEXT || g_ends_ci(name, ".txt") || g_ends_ci(name, ".cfg") || g_ends_ci(name, ".ini") || g_ends_ci(name, ".xml");
}
static void search_files(GWindow *w)
{
    files_ready(w);
    pool.result_count = 0; pool.result_top = 0; pool.result_selected = 0;
    const char *needle = S->search.text;
    int n = g_len(needle);
    if (!n) return;
    for (int f = 0; f < pool.file_count && pool.result_count < RESULTS_MAX; f++) {
        if (!text_file(pool.files[f])) continue;
        char path[300]; file_path(w, f, path, sizeof(path));
        int size = g_read_file(path, pool.scratch, 256 * 1024 - 1);
        if (size <= 0) continue;
        pool.scratch[size] = 0;
        int line = 1, start = 0;
        for (int i = 0; i + n <= size && pool.result_count < RESULTS_MAX; i++) {
            if (pool.scratch[i] == '\n') { line++; start = i + 1; continue; }
            int k = 0;
            while (k < n && (S->search_case ? pool.scratch[i + k] == needle[k] :
                   (pool.scratch[i + k] | (pool.scratch[i + k] >= 'A' && pool.scratch[i + k] <= 'Z' ? 32 : 0)) == (needle[k] | (needle[k] >= 'A' && needle[k] <= 'Z' ? 32 : 0)))) k++;
            if (k < n) continue;
            Result *r = &pool.results[pool.result_count++];
            r->file = (short)f; r->line = line;
            int s = start; while (pool.scratch[s] == ' ') s++;
            int m = 0; while (s < size && pool.scratch[s] != '\n' && m < 90) r->preview[m++] = pool.scratch[s++];
            r->preview[m] = 0;
            while (i < size && pool.scratch[i] != '\n') i++;
            line++; start = i + 1;
        }
    }
    char text[80]; g_format(text, sizeof(text), g_tr("%d result(s)"), pool.result_count); g_copy(S->status, text, sizeof(S->status));
}

/* ---------------------------------------------------------------- Quick Open and the Command Palette */
enum { K_NEW = 1, K_OPEN_FILE, K_OPEN_FOLDER, K_SAVE, K_SAVE_AS, K_SAVE_ALL, K_CLOSE, K_RUN, K_TERMINAL, K_PROBLEMS,
       K_OUTPUT, K_SIDEBAR, K_PANEL, K_EXPLORER, K_SEARCH, K_FIND, K_REPLACE, K_GOTO, K_QUICK, K_COMMENT, K_DUPLICATE,
       K_DELETE_LINE, K_UNDO, K_REDO, K_REVEAL, K_REFRESH, K_DOCS, K_NEW_FILE, K_NEW_FOLDER, K_BROWSER, K_ABOUT,
       K_DEBUG, K_BREAK, K_SUGGEST, K_COUNT };
static const struct { int id; const char *name, *keys; } commands[] = {
    {K_DEBUG, "Debug: Start Debugging (Python)", "F5"}, {K_BREAK, "Debug: Toggle Breakpoint", "F9"},
    {K_SUGGEST, "Trigger Suggest", "Ctrl+Space"},
    {K_NEW, "File: New Untitled File", "Ctrl+N"}, {K_OPEN_FILE, "File: Open File...", "Ctrl+O"},
    {K_OPEN_FOLDER, "File: Open Folder...", "Ctrl+K Ctrl+O"}, {K_SAVE, "File: Save", "Ctrl+S"},
    {K_SAVE_AS, "File: Save As...", "Ctrl+Shift+S"}, {K_SAVE_ALL, "File: Save All", "Ctrl+K S"},
    {K_CLOSE, "View: Close Editor", "Ctrl+W"}, {K_RUN, "Run: Run Without Debugging", "Ctrl+F5"},
    {K_TERMINAL, "View: Toggle Terminal", "Ctrl+`"}, {K_PROBLEMS, "View: Show Problems", "Ctrl+Shift+M"},
    {K_OUTPUT, "View: Show Output", "Ctrl+Shift+U"}, {K_SIDEBAR, "View: Toggle Side Bar", "Ctrl+B"},
    {K_PANEL, "View: Toggle Panel", "Ctrl+J"}, {K_EXPLORER, "View: Show Explorer", "Ctrl+Shift+E"},
    {K_SEARCH, "Search: Find in Files", "Ctrl+Shift+F"}, {K_FIND, "Edit: Find", "Ctrl+F"},
    {K_REPLACE, "Edit: Replace", "Ctrl+H"}, {K_GOTO, "Go to Line...", "Ctrl+G"}, {K_QUICK, "Go to File...", "Ctrl+P"},
    {K_COMMENT, "Toggle Line Comment", "Ctrl+/"}, {K_DUPLICATE, "Duplicate Line", "Ctrl+Shift+D"},
    {K_DELETE_LINE, "Delete Line", "Ctrl+Shift+K"}, {K_UNDO, "Undo", "Ctrl+Z"}, {K_REDO, "Redo", "Ctrl+Y"},
    {K_REVEAL, "File: Reveal Active File in Explorer", ""}, {K_REFRESH, "Explorer: Refresh", ""},
    {K_NEW_FILE, "Explorer: New File...", ""}, {K_NEW_FOLDER, "Explorer: New Folder...", ""},
    {K_DOCS, "Help: Language Manual", "F1"}, {K_BROWSER, "Open the Folder in Gouse Web", ""},
    {K_ABOUT, "Help: About Barnix Code", ""},
};
#define COMMAND_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))
static int fuzzy(const char *text, const char *query, int *score)
{
    int t = 0, q = 0, s = 0, run = 0;
    while (text[t] && query[q]) {
        char a = text[t], b = query[q];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a == b) { q++; run++; s += 1 + run; } else run = 0;
        t++;
    }
    *score = s;
    return !query[q];
}
static void overlay_filter(GWindow *w)
{
    State *s = S;
    int scores[400];
    s->overlay_count = 0;
    if (s->overlay == OV_GOTO) return;
    int total = s->overlay == OV_QUICK ? pool.file_count : COMMAND_COUNT;
    for (int i = 0; i < total && s->overlay_count < 400; i++) {
        const char *text = s->overlay == OV_QUICK ? pool.files[i] : g_tr(commands[i].name);
        int score;
        if (!fuzzy(text, s->overlay_input.text, &score)) continue;
        if (s->overlay == OV_QUICK) { int b; if (fuzzy(base_name(text), s->overlay_input.text, &b)) score += 20 + b; }
        int at = s->overlay_count++;
        while (at > 0 && scores[at - 1] < score) { scores[at] = scores[at - 1]; s->overlay_items[at] = s->overlay_items[at - 1]; at--; }
        scores[at] = score; s->overlay_items[at] = i;
    }
    s->overlay_selected = 0; s->overlay_top = 0;
}
static void overlay_open(GWindow *w, int kind)
{
    if (kind == OV_QUICK) files_ready(w);
    S->overlay = kind;
    g_edit_set(&S->overlay_input, "");
    overlay_filter(w);
}
static void command(GWindow *w, int id);
static void overlay_accept(GWindow *w)
{
    State *s = S;
    int kind = s->overlay;
    s->overlay = OV_NONE;
    if (kind == OV_GOTO) { Doc *d = active_doc(w); if (d) { gte_goto_line(&d->edit, g_atoi(s->overlay_input.text) - 1); d->edit.shown = -1; } return; }
    if (!s->overlay_count) return;
    int i = s->overlay_items[s->overlay_selected];
    if (kind == OV_QUICK) { char path[300]; file_path(w, i, path, sizeof(path)); open_file(w, path); }
    else command(w, commands[i].id);
}

/* ---------------------------------------------------------------- commands */
static void manual(GWindow *w)
{
    Doc *d = active_doc(w);
    int l = d ? d->lang : LANG_TEXT;
    g_browse(l == LANG_C ? "about:docs/c" : l == LANG_CPP ? "about:docs/cpp" : l == LANG_PY ? "about:docs/python" :
             l == LANG_NOVA ? "about:docs/nova" : l == LANG_HTML || l == LANG_CSS || l == LANG_JS ? "about:docs/web" : "about:docs/ide");
}
static void command(GWindow *w, int id)
{
    State *s = S;
    Doc *d = active_doc(w);
    switch (id) {
    case K_NEW: open_file(w, ""); break;
    case K_OPEN_FILE: { char path[256]; g_path_join(path, sizeof(path), s->folder, ""); g_prompt(w, P_SAVE_AS + 50, "Open File", "File path:", path); break; }
    case K_OPEN_FOLDER: g_prompt(w, P_FOLDER, "Open Folder", "Folder:", s->folder); break;
    case K_SAVE: save_active(w); break;
    case K_SAVE_AS: if (d) ask_save_as(w, d); break;
    case K_SAVE_ALL: for (int i = 0; i < DOCS; i++) if (pool.docs[i] && pool.docs[i]->used && pool.docs[i]->edit.modified && pool.docs[i]->path[0]) save_doc(w, pool.docs[i]); break;
    case K_CLOSE: if (d) close_doc(w, s->active, 0); break;
    case K_RUN: run_active(w); break;
    case K_DEBUG: debug_active(w); break;
    case K_BREAK: if (d) toggle_break(d, gte_line_of(&d->edit, d->edit.cursor) + 1); break;
    case K_SUGGEST: if (d) complete_update(d, 1); break;
    case K_TERMINAL: if (s->panel && s->panel_tab == PANEL_TERMINAL && s->focus == F_TERMINAL) { s->panel = 0; s->focus = F_EDITOR; } else { s->panel = 1; s->panel_tab = PANEL_TERMINAL; s->focus = F_TERMINAL; } break;
    case K_PROBLEMS: s->panel = 1; s->panel_tab = PANEL_PROBLEMS; break;
    case K_OUTPUT: s->panel = 1; s->panel_tab = PANEL_OUTPUT; break;
    case K_SIDEBAR: s->sidebar = !s->sidebar; break;
    case K_PANEL: s->panel = !s->panel; if (!s->panel && s->focus == F_TERMINAL) s->focus = F_EDITOR; break;
    case K_EXPLORER: s->sidebar = 1; s->side = SIDE_EXPLORER; s->focus = F_TREE; break;
    case K_SEARCH: s->sidebar = 1; s->side = SIDE_SEARCH; s->focus = F_SEARCH; break;
    case K_FIND: case K_REPLACE:
        s->find_open = 1; s->replace_open = id == K_REPLACE; s->focus = F_FIND;
        if (d) { int a, b; if (gte_selection(&d->edit, &a, &b) && b - a < 200) { char t[256]; copy_text(t, d->text + a, b - a); t[b - a] = 0; int nl = 0; for (int k = 0; t[k]; k++) if (t[k] == '\n') nl = 1; if (!nl) g_edit_set(&s->find, t); } }
        break;
    case K_GOTO: overlay_open(w, OV_GOTO); break;
    case K_QUICK: overlay_open(w, OV_QUICK); break;
    case K_COMMENT: if (d) { snapshot(d, 1); toggle_comment(d); } break;
    case K_DUPLICATE: if (d) { snapshot(d, 1); duplicate_lines(d); } break;
    case K_DELETE_LINE: if (d) { snapshot(d, 1); delete_lines(d); } break;
    case K_UNDO: if (d) undo(d); break;
    case K_REDO: if (d) redo(d); break;
    case K_REVEAL: if (d && d->path[0]) { for (int i = 0; i < pool.tree_count; i++) if (g_equal(pool.tree[i].path, d->path)) pool.tree_selected = i; s->sidebar = 1; s->side = SIDE_EXPLORER; } break;
    case K_REFRESH: tree_load(w); break;
    case K_NEW_FILE: g_prompt(w, P_NEW_FILE, "New File", "File name:", ""); break;
    case K_NEW_FOLDER: g_prompt(w, P_NEW_FOLDER, "New Folder", "Folder name:", ""); break;
    case K_DOCS: manual(w); break;
    case K_BROWSER: { char url[300]; g_format(url, sizeof(url), "file://%s/", s->folder); g_browse(url); break; }
    case K_ABOUT: g_message(w, "Barnix Code", "Barnix Code 1.0 for Happy Gouse\nA code editor in the manner of VS Code:\nCtrl+P files, Ctrl+Shift+P commands,\nF5 runs, Ctrl+` the terminal."); break;
    }
    save_session(w);
}

/* ---------------------------------------------------------------- window */
static void open(GWindow *w, const char *argument)
{
    State *s = S;
    if (!pool_ready()) { g_error(w, "Barnix Code", "Not enough memory for Barnix Code."); return; }
    s->active = -1; s->sidebar = 1; s->panel = 1; s->panel_h = 170; s->panel_tab = PANEL_TERMINAL;
    g_copy(s->folder, g_home(), sizeof(s->folder));
    load_session(w);
    g_copy(s->cwd, s->folder, sizeof(s->cwd));
    tree_load(w);
    if (argument && argument[0]) open_file(w, argument);
    if (s->active < 0) for (int i = 0; i < DOCS; i++) if (pool.docs[i] && pool.docs[i]->used) { s->active = i; break; }
    term_out(g_tr("Barnix Code terminal. Programs run here; \"code FILE\" opens a file."));
    set_status(w, "Ctrl+P: go to file   Ctrl+Shift+P: commands   F5: run");
    /* a large window from the start */
    int width = g_screen_w - 60 < 1100 ? g_screen_w - 60 : 1100, height = g_screen_h - 100 < 720 ? g_screen_h - 100 : 720;
    if (width > w->w || height > w->h) g_resize(w, width, height);
}
static void draw_input(BgxCanvas *c, int x, int y, int width, const GEdit *e, int focused, const char *placeholder)
{
    bgx_fill(c, x, y, width, 22, C_INPUT);
    if (focused) bgx_frame(c, x, y, width, 22, 1, 0x007fd4);
    if (!e->text[0] && placeholder) g_text_fit(c, x + 5, y + 3, width - 10, placeholder, C_DIM);
    int before = 0, first = 0, columns = (width - 10) / 8;
    for (int i = 0; i < e->cursor; i++) if (((unsigned char)e->text[i] & 0xc0) != 0x80) before++;
    int skip = before > columns - 1 ? before - columns + 1 : 0, seen = 0;
    while (e->text[first] && seen < skip) { first = g_utf8_next(e->text, g_len(e->text), first); seen++; }
    g_text_fit_raw(c, x + 5, y + 3, width - 10, e->text + first, C_TEXT);
    if (focused && (g_now() / 500) % 2 == 0) bgx_fill(c, x + 5 + (before - skip) * 8, y + 3, 1, 16, C_BRIGHT);
}
static void draw_magnifier(BgxCanvas *c, int x, int y, unsigned int color) { g_ring(c, x + 9, y + 9, 6, color); g_thick_line(c, x + 13, y + 13, x + 19, y + 19, 2, color); }
static void draw_files_icon(BgxCanvas *c, int x, int y, unsigned int color)
{
    bgx_frame(c, x + 5, y + 2, 11, 15, 1, color); bgx_frame(c, x + 9, y + 6, 11, 15, 1, color); bgx_fill(c, x + 10, y + 7, 9, 13, C_ACTIVITY);
    bgx_frame(c, x + 9, y + 6, 11, 15, 1, color);
}
static void draw_run_icon(BgxCanvas *c, int x, int y, unsigned int color)
{
    for (int i = 0; i < 9; i++) g_line(c, x + 6 + i, y + 3 + i, x + 6 + i, y + 21 - i, color);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    if (!pool.ready) { bgx_fill(c, 0, 0, w->w, w->h, C_EDITOR); return; }
    int sw, ex, ey, ew, eh, py;
    layout(w, &sw, &ex, &ey, &ew, &eh, &py);
    Doc *d = active_doc(w);
    /* activity bar */
    bgx_fill(c, 0, 0, 44, w->h - 22, C_ACTIVITY);
    for (int i = 0; i < 3; i++) {
        int active = s->sidebar && s->side == i, y = 10 + i * 48;
        unsigned int color = active ? C_BRIGHT : C_DIM;
        if (active) bgx_fill(c, 0, y - 6, 2, 36, C_BRIGHT);
        if (i == 0) draw_files_icon(c, 10, y, color); else if (i == 1) draw_magnifier(c, 11, y + 1, color); else draw_run_icon(c, 12, y, color);
    }
    /* side bar */
    if (sw) {
        bgx_fill(c, 44, 0, sw, w->h - 22, C_SIDEBAR);
        static const char *const titles[] = {"EXPLORER", "SEARCH", "RUN"};
        g_text(c, 56, 8, titles[s->side], C_HEADER);
        if (s->side == SIDE_EXPLORER) {
            char folder[80]; g_copy(folder, base_name(s->folder)[0] ? base_name(s->folder) : "/", sizeof(folder));
            for (char *p = folder; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            g_text_fit_raw(c, 56, 32, sw - 70, folder, C_TEXT);
            /* new file, new folder */
            bgx_frame(c, 44 + sw - 48, 31, 10, 13, 1, C_DIM); g_line(c, 44 + sw - 40, 40, 44 + sw - 40, 46, C_TEXT); g_line(c, 44 + sw - 43, 43, 44 + sw - 37, 43, C_TEXT);
            bgx_frame(c, 44 + sw - 30, 33, 14, 10, 1, C_DIM); bgx_fill(c, 44 + sw - 30, 31, 6, 2, C_DIM);
            g_line(c, 44 + sw - 18, 40, 44 + sw - 18, 46, C_TEXT); g_line(c, 44 + sw - 21, 43, 44 + sw - 15, 43, C_TEXT);
            int rows = (w->h - 22 - 56) / ROW;
            if (pool.tree_selected < pool.tree_top) pool.tree_top = pool.tree_selected;
            if (pool.tree_selected >= pool.tree_top + rows) pool.tree_top = pool.tree_selected - rows + 1;
            for (int r = 0; r < rows && pool.tree_top + r < pool.tree_count; r++) {
                int i = pool.tree_top + r, y = 54 + r * ROW;
                Node *n = &pool.tree[i];
                int open_here = d && g_equal(d->path, n->path);
                if (i == pool.tree_selected) bgx_fill(c, 44, y, sw, ROW, s->focus == F_TREE ? C_SELECTED : 0x37373d);
                else if (open_here) bgx_fill(c, 44, y, sw, ROW, C_HOVER);
                int x = 52 + n->depth * 12;
                if (n->dir) {
                    if (n->open) { for (int k = 0; k < 4; k++) g_line(c, x + 1 + k, y + 8 + k, x + 8 - k, y + 8 + k, C_TEXT); }
                    else { for (int k = 0; k < 4; k++) g_line(c, x + 2 + k, y + 6 + k, x + 2 + k, y + 13 - k, C_TEXT); }
                }
                int lang = n->dir ? -1 : lang_of(n->path);
                unsigned int dot = lang == LANG_C || lang == LANG_CPP ? 0x519aba : lang == LANG_PY ? 0xffd43b : lang == LANG_HTML ? 0xe37933 :
                                   lang == LANG_CSS ? 0x42a5f5 : lang == LANG_JS ? 0xcbcb41 : lang == LANG_MD ? 0x519aba : lang == LANG_JSON ? 0xcbcb41 : C_DIM;
                if (!n->dir) bgx_fill(c, x + 2, y + 6, 6, 8, dot);
                g_text_fit_raw(c, x + 14, y + 2, sw - (x - 44) - 20, base_name(n->path), n->dir ? C_TEXT : 0xcccccc);
            }
        } else if (s->side == SIDE_SEARCH) {
            draw_input(c, 54, 30, sw - 20, &s->search, s->focus == F_SEARCH, g_tr("Search (Enter)"));
            g_text(c, 56, 58, s->search_case ? "[Aa] match case" : "[aa] any case", C_DIM);
            int rows = (w->h - 22 - 84) / 34;
            if (pool.result_selected < pool.result_top) pool.result_top = pool.result_selected;
            if (pool.result_selected >= pool.result_top + rows) pool.result_top = pool.result_selected - rows + 1;
            for (int r = 0; r < rows && pool.result_top + r < pool.result_count; r++) {
                int i = pool.result_top + r, y = 80 + r * 34;
                Result *res = &pool.results[i];
                if (i == pool.result_selected) bgx_fill(c, 44, y, sw, 34, C_SELECTED);
                char where[200]; g_format(where, sizeof(where), "%s:%d", pool.files[res->file], res->line);
                g_text_fit_raw(c, 54, y + 1, sw - 16, where, 0x9cdcfe);
                g_text_fit_raw(c, 62, y + 17, sw - 24, res->preview, C_TEXT);
            }
        } else {
            static const char *const lines[] = {"F5 runs the current file:", "", "C, C++   built by GCC, run", "Python   the debugger", "HTML     opens in Gouse Web",
                "Nova     its .jar on BJVM", "", "Ctrl+F5 runs without it,", "F9 or the gutter: breakpoint.", "Programs use the Terminal."};
            for (int i = 0; i < 10; i++) g_text_fit(c, 56, 34 + i * 18, sw - 20, lines[i], i ? C_TEXT : C_HEADER);
            bgx_fill(c, 56, 230, sw - 28, 28, C_ACCENT);
            g_text_center(c, 56, 236, sw - 28, "Run (F5)", C_BRIGHT);
        }
    }
    /* tabs */
    bgx_fill(c, ex, 0, ew, 30, 0x252526);
    int tx = ex;
    for (int i = 0; i < DOCS; i++) {
        s->tab_w[i] = 0;
        if (!pool.docs[i] || !pool.docs[i]->used) continue;
        char title[64]; doc_title(pool.docs[i], title, sizeof(title));
        int width = g_len(title) * 8 + 46;
        if (tx + width > ex + ew) break;
        int active = i == s->active;
        bgx_fill(c, tx, 0, width, 30, active ? C_EDITOR : C_TAB);
        if (active) bgx_fill(c, tx, 0, width, 1, 0x0078d4);
        bgx_fill(c, tx + width - 1, 0, 1, 30, 0x252526);
        g_text_raw(c, tx + 12, 7, title, active ? C_BRIGHT : 0x969696);
        if (pool.docs[i]->edit.modified) g_disc(c, tx + width - 18, 15, 4, active ? C_BRIGHT : 0x969696);
        else g_text_raw(c, tx + width - 22, 7, "x", active ? 0xcccccc : 0x6e6e6e);
        s->tab_x[i] = tx; s->tab_w[i] = width;
        tx += width;
    }
    /* editor */
    if (d) {
        hl_lang = d->lang;
        update_marks(d);
        int mini = ew >= 500 ? 70 : 0;
        gte_draw(&d->edit, c, ex, ey, ew - mini, eh, g_focused(w) && s->focus == F_EDITOR && s->overlay == OV_NONE);
        if (mini) {   /* the minimap: every line a stroke as long as its text */
            int mx = ex + ew - mini;
            bgx_fill(c, mx, ey, mini, eh, C_EDITOR);
            bgx_fill(c, mx, ey, 1, eh, 0x2b2b2b);
            int total = gte_line_of(&d->edit, d->edit.length) + 1;
            int scale = total * 2 > eh ? (total * 2 + eh - 1) / eh : 1;
            int rows = (eh - 4) / 16;
            int top = d->edit.top, view_y = ey + top * 2 / scale, view_h = rows * 2 / scale;
            if (view_h < 6) view_h = 6;
            bgx_blend(c, mx + 1, view_y, mini - 1, view_h, 0x797979, 40);
            int line = 0, col = 0, lead = -1;
            for (int p = 0; p <= d->edit.length && line / scale * 2 < eh; p++) {
                if (p == d->edit.length || d->text[p] == '\n') {
                    if (lead >= 0 && line % scale == 0) {
                        int length = col - lead; if (length > 60) length = 60;
                        if (length > 0) bgx_fill(c, mx + 6 + lead / 2 % 30, ey + line / scale * 2, length / 2 + 1, 1, 0x6d6d6d);
                    }
                    line++; col = 0; lead = -1;
                } else { if (lead < 0 && d->text[p] != ' ') lead = col; col++; }
            }
        }
        if (s->focus == F_EDITOR && s->overlay == OV_NONE) draw_completion(w, c, d, ex, ey, ew - mini, eh);
        /* find widget */
        if (s->find_open) {
            int fw = 360, fx = ex + ew - fw - 84, fy = ey + 2, fh = s->replace_open ? 58 : 32;
            bgx_fill(c, fx, fy, fw, fh, 0x252526);
            bgx_frame(c, fx, fy, fw, fh, 1, 0x454545);
            draw_input(c, fx + 6, fy + 5, 200, &s->find, s->focus == F_FIND, g_tr("Find"));
            char count[48];
            int total = count_matches(d, s->find.text, s->match_case);
            g_format(count, sizeof(count), total ? g_tr("%d found") : g_tr("No results"), total);
            g_text_fit(c, fx + 212, fy + 8, 90, count, total || !s->find.text[0] ? C_TEXT : C_ERROR);
            g_text_raw(c, fx + 304, fy + 8, s->match_case ? "Aa" : "aa", s->match_case ? 0x4daafc : C_DIM);
            g_text_raw(c, fx + 330, fy + 8, "x", C_TEXT);
            if (s->replace_open) {
                draw_input(c, fx + 6, fy + 31, 200, &s->replace, s->focus == F_REPLACE, g_tr("Replace"));
                g_text_raw(c, fx + 212, fy + 34, "Enter: one   All", C_DIM);
            }
        }
    } else {   /* the welcome page */
        bgx_fill(c, ex, ey, ew, eh, C_EDITOR);
        int cx = ex + ew / 2 - 160, cy = ey + eh / 2 - 120;
        bgx_text(c, cx, cy, "Barnix Code", 0xcccccc, 3);
        g_text(c, cx, cy + 56, "Editing evolved, on Barnix", C_DIM);
        static const char *const tips[] = {"New File            Ctrl+N", "Open File           Ctrl+O", "Go to File          Ctrl+P",
            "Show All Commands   Ctrl+Shift+P", "Open Folder         Ctrl+K Ctrl+O", "Toggle Terminal     Ctrl+`"};
        for (int i = 0; i < 6; i++) g_text(c, cx, cy + 96 + i * 22, tips[i], i == 3 ? 0x4daafc : C_TEXT);
    }
    /* bottom panel */
    if (s->panel) {
        int ph = w->h - 22 - py;
        bgx_fill(c, ex, py, ew, ph, C_EDITOR);
        bgx_fill(c, ex, py, ew, 1, 0x414141);
        static const char *const tabs[] = {"PROBLEMS", "OUTPUT", "TERMINAL"};
        int x = ex + 10;
        for (int i = 0; i < 3; i++) {
            int width = bgx_text_width(tabs[i], 1) + 16;
            g_text(c, x + 8, py + 4, tabs[i], s->panel_tab == i ? C_BRIGHT : C_DIM);
            if (i == PANEL_PROBLEMS && pool.problem_count) {
                char n[8]; g_format(n, sizeof(n), "%d", pool.problem_count);
                g_disc(c, x + width + 6, py + 12, 8, 0x4d4d4d); g_text_raw(c, x + width + 3 - (pool.problem_count > 9 ? 4 : 0), py + 4, n, C_BRIGHT);
                width += 18;
            }
            if (s->panel_tab == i) bgx_fill(c, x + 8, py + 22, width - 16, 1, 0xe7e7e7);
            x += width + 8;
        }
        g_text_raw(c, ex + ew - 20, py + 4, "x", C_DIM);
        int ly = py + 26, rows = (ph - 30) / 16;
        if (s->panel_tab == PANEL_PROBLEMS) {
            if (!pool.problem_count) g_text(c, ex + 16, ly + 4, "No problems have been detected.", C_DIM);
            for (int r = 0; r < rows && pool.problem_top + r < pool.problem_count; r++) {
                Problem *p = &pool.problems[pool.problem_top + r];
                int y = ly + r * 18;
                g_disc(c, ex + 22, y + 8, 5, p->error ? C_ERROR : C_WARNING);
                char text[400]; g_format(text, sizeof(text), "%s  %s [%d, %d]", p->message, base_name(p->file), p->line, p->column);
                g_text_fit_raw(c, ex + 34, y, ew - 50, text, C_TEXT);
            }
        } else if (s->panel_tab == PANEL_OUTPUT) {
            int line = 0, y = ly;
            for (int p = 0; p < pool.output_length && y + 16 <= w->h - 24;) {
                int e = p; while (e < pool.output_length && pool.output[e] != '\n') e++;
                if (line >= pool.output_top) {
                    char text[300]; int n = e - p < 299 ? e - p : 299; copy_text(text, pool.output + p, n); text[n] = 0;
                    g_text_fit_raw(c, ex + 12, y, ew - 24, text, C_TEXT); y += 16;
                }
                line++; p = e + 1;
            }
        } else {
            int input_row = rows - 1;
            int first = pool.term_count - input_row - pool.term_scroll;
            if (first < 0) first = 0;
            for (int r = 0; r < input_row && first + r < pool.term_count; r++) g_text_raw(c, ex + 12, ly + r * 16, pool.term[first + r], 0xcccccc);
            int y = ly + (pool.term_count - first < input_row ? pool.term_count - first : input_row) * 16;
            char prompt[300]; g_format(prompt, sizeof(prompt), "%s$ ", s->cwd);
            int px = ex + 12 + g_text_raw(c, ex + 12, y, prompt, 0x4ec9b0);
            g_text_fit_raw(c, px, y, ew - (px - ex) - 10, s->terminal.text, C_BRIGHT);
            if (s->focus == F_TERMINAL && g_focused(w) && (g_now() / 500) % 2 == 0) {
                int before = 0; for (int i = 0; i < s->terminal.cursor; i++) if (((unsigned char)s->terminal.text[i] & 0xc0) != 0x80) before++;
                bgx_fill(c, px + before * 8, y + 1, 8, 15, 0xcccccc);
            }
        }
    }
    /* status bar */
    bgx_fill(c, 0, w->h - 22, w->w, 22, C_STATUS);
    int errors = 0, warnings = 0;
    for (int i = 0; i < pool.problem_count; i++) { if (pool.problems[i].error) errors++; else warnings++; }
    char left[200]; g_format(left, sizeof(left), " %s   %s %d  %s %d   %s", base_name(s->folder)[0] ? base_name(s->folder) : "/", g_tr("errors"), errors, g_tr("warnings"), warnings, s->status);
    if (d) {
        int line = gte_line_of(&d->edit, d->edit.cursor) + 1, column = 1;
        for (int p = gte_line_start(&d->edit, line - 1); p < d->edit.cursor; p = g_utf8_next(d->text, d->edit.length, p)) column++;
        char right[120]; g_format(right, sizeof(right), "Ln %d, Col %d   Spaces: 4   UTF-8   LF   %s ", line, column, langs[d->lang].name);
        int rw = g_len(right) * 8;
        g_text_fit_raw(c, 4, w->h - 19, w->w - rw - 16, left, C_BRIGHT);
        g_text_raw(c, w->w - rw - 4, w->h - 19, right, C_BRIGHT);
    } else g_text_fit_raw(c, 4, w->h - 19, w->w - 8, left, C_BRIGHT);
    /* Quick Open, the Command Palette, Go to Line */
    if (s->overlay) {
        int ow = ew - 80 < 600 ? ew - 80 : 600, ox = ex + (ew - ow) / 2, oy = 4;
        int rows = s->overlay == OV_GOTO ? 0 : (s->overlay_count < 12 ? s->overlay_count : 12);
        bgx_fill(c, ox, oy, ow, 36 + rows * ROW + 6, 0x252526);
        bgx_frame(c, ox, oy, ow, 36 + rows * ROW + 6, 1, 0x454545);
        const char *hint = s->overlay == OV_QUICK ? "Search files by name" : s->overlay == OV_COMMANDS ? "Type a command" : "Line number, then Enter";
        GEdit shown = s->overlay_input;
        if (s->overlay == OV_COMMANDS) { char t[256] = ">"; g_append(t, shown.text, sizeof(t)); g_edit_set(&shown, t); shown.cursor = s->overlay_input.cursor + 1; }
        draw_input(c, ox + 6, oy + 6, ow - 12, &shown, 1, g_tr(hint));
        if (s->overlay_selected < s->overlay_top) s->overlay_top = s->overlay_selected;
        if (s->overlay_selected >= s->overlay_top + 12) s->overlay_top = s->overlay_selected - 11;
        for (int r = 0; r < rows && s->overlay_top + r < s->overlay_count; r++) {
            int i = s->overlay_items[s->overlay_top + r], y = oy + 36 + r * ROW;
            if (s->overlay_top + r == s->overlay_selected) bgx_fill(c, ox + 1, y, ow - 2, ROW, C_SELECTED);
            if (s->overlay == OV_QUICK) {
                g_text_fit_raw(c, ox + 12, y + 2, ow / 2, base_name(pool.files[i]), C_BRIGHT);
                g_text_fit_raw(c, ox + 20 + g_len(base_name(pool.files[i])) * 8, y + 2, ow / 2, pool.files[i], C_DIM);
            } else {
                g_text_fit(c, ox + 12, y + 2, ow - 160, commands[i].name, C_TEXT);
                g_text_raw(c, ox + ow - 12 - g_len(commands[i].keys) * 8, y + 2, commands[i].keys, C_DIM);
            }
        }
    }
    /* title */
    char title[96];
    if (d) { char name[64]; doc_title(d, name, sizeof(name)); g_format(title, sizeof(title), "%s%s - Barnix Code", d->edit.modified ? "* " : "", name); }
    else g_copy(title, "Barnix Code", sizeof(title));
    if (!g_equal(w->title, title)) g_set_title(w, title);
}

/* ---------------------------------------------------------------- keys */
static int typing(const GEvent *e)
{
    unsigned int k = e->key;
    return k == '\b' || k == '\n' || e->code == 0xd3 || k == 22 || k == 24 || (k >= 32 && k != 127 && !(k >= 0x100 && k <= 0x108));
}
static void editor_key_inner(GWindow *w, Doc *d, GEvent *e);
static void editor_key(GWindow *w, Doc *d, GEvent *e)
{
    unsigned int k = e->key;
    if (g_ctrl() && e->code == 0x39) { complete_update(d, 1); return; }   /* Ctrl+Space */
    if (pool.comp_open) {
        if (e->code == BGX_KEY_UP) { if (pool.comp_selected > 0) pool.comp_selected--; else pool.comp_selected = pool.comp_count - 1; return; }
        if (e->code == BGX_KEY_DOWN) { pool.comp_selected = (pool.comp_selected + 1) % pool.comp_count; return; }
        if (e->code == 0xc9) { pool.comp_selected -= 8; if (pool.comp_selected < 0) pool.comp_selected = 0; return; }
        if (e->code == 0xd1) { pool.comp_selected += 8; if (pool.comp_selected >= pool.comp_count) pool.comp_selected = pool.comp_count - 1; return; }
        if (e->code == BGX_KEY_TAB || k == '\n') { complete_accept(d); return; }
        if (e->code == BGX_KEY_ESC) { complete_close(); return; }
    }
    editor_key_inner(w, d, e);
    /* the list follows the typing */
    if ((k < 128 && is_word((char)k)) || (d->lang == LANG_CSS && k == '-')) complete_update(d, 0);
    else if (k == '.' || (k == ':' && d->edit.cursor > 1 && d->text[d->edit.cursor - 2] == ':')) complete_update(d, 0);
    else if (k == '\b' && pool.comp_open) complete_update(d, 0);
    else if (k == '<' && d->lang == LANG_HTML) complete_update(d, 1);
    else complete_close();
}
static void editor_key_inner(GWindow *w, Doc *d, GEvent *e)
{
    (void)w;
    unsigned int k = e->key;
    int ctrl = g_ctrl(), shift = g_shift(), alt = g_alt();
    if (alt && (e->code == BGX_KEY_UP || e->code == BGX_KEY_DOWN)) { snapshot(d, 1); move_lines(d, e->code == BGX_KEY_DOWN); return; }
    if (e->code == 0xc7 && !ctrl) {   /* Home: the first character, then the line start */
        int start = line_start_of(d, d->edit.cursor), first = start;
        while (first < d->edit.length && d->text[first] == ' ') first++;
        int target = d->edit.cursor == first ? start : first;
        if (shift) { if (d->edit.anchor < 0) d->edit.anchor = d->edit.cursor; } else d->edit.anchor = -1;
        d->edit.cursor = target; d->edit.goal = target - start;
        return;
    }
    if (e->code == BGX_KEY_TAB) {
        int a, b;
        if (shift || (gte_selection(&d->edit, &a, &b) && line_start_of(d, a) != line_start_of(d, b))) { snapshot(d, 1); indent_lines(d, shift); return; }
        snapshot(d, 0);
        int col = d->edit.cursor - line_start_of(d, d->edit.cursor);
        gte_insert(&d->edit, "    ", 4 - col % 4);
        return;
    }
    if (typing(e)) snapshot(d, k == '\n' || k == 22 || k == 24 || k == ' ');
    if (k == '\n') { new_line(d); return; }
    if (k == '\b' && d->edit.anchor < 0 && d->edit.cursor > 0 && d->edit.cursor < d->edit.length) {   /* a pair goes together */
        char a = d->text[d->edit.cursor - 1], b = d->text[d->edit.cursor];
        if ((a == '(' && b == ')') || (a == '[' && b == ']') || (a == '{' && b == '}') || (a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            replace_range(d, d->edit.cursor - 1, d->edit.cursor + 1, "", 0);
            return;
        }
    }
    if (k == ')' || k == ']' || k == '}' || k == '"' || k == '\'' || k == '`') {   /* typed over the closing one */
        if (d->edit.anchor < 0 && d->edit.cursor < d->edit.length && d->text[d->edit.cursor] == (char)k) { d->edit.cursor++; return; }
    }
    if (k == '(' || k == '[' || k == '{' || k == '"' || k == '\'' || (k == '`' && d->lang == LANG_JS)) {
        char close = k == '(' ? ')' : k == '[' ? ']' : k == '{' ? '}' : (char)k;
        int a, b;
        if (gte_selection(&d->edit, &a, &b)) {   /* wraps the selection */
            static char block[TEXT_MAX]; int n = 0;
            block[n++] = (char)k; copy_text(block + n, d->text + a, b - a); n += b - a; block[n++] = close;
            replace_range(d, a, b, block, n);
            d->edit.anchor = a + 1; d->edit.cursor = b + 1;
            return;
        }
        char next = d->edit.cursor < d->edit.length ? d->text[d->edit.cursor] : 0;
        char prev = d->edit.cursor > 0 ? d->text[d->edit.cursor - 1] : 0;
        int quote = k == '"' || k == '\'' || k == '`';
        if (!is_word(next) && !(quote && is_word(prev))) {
            char pair[2] = {(char)k, close};
            gte_insert(&d->edit, pair, 2);
            d->edit.cursor--;
            return;
        }
    }
    gte_key(&d->edit, e, 20);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (!pool.ready) return;
    int sw, ex, ey, ew, eh, py;
    layout(w, &sw, &ex, &ey, &ew, &eh, &py);
    Doc *d = active_doc(w);
    if (e->type == EV_KEY) {
        int ctrl = g_ctrl(), shift = g_shift();
        unsigned int code = e->code;
        static int chord;   /* Ctrl+K, then a key */
        if (s->overlay) {
            if (code == BGX_KEY_ESC) { s->overlay = OV_NONE; return; }
            if (code == BGX_KEY_UP && s->overlay_selected > 0) { s->overlay_selected--; return; }
            if (code == BGX_KEY_DOWN && s->overlay_selected + 1 < s->overlay_count) { s->overlay_selected++; return; }
            int r = g_edit_key(&s->overlay_input, e);
            if (r == 2) overlay_accept(w);
            else if (r == 1) overlay_filter(w);
            return;
        }
        if (chord) {
            chord = 0;
            if (ctrl && code == 0x18) { command(w, K_OPEN_FOLDER); return; }   /* Ctrl+K Ctrl+O */
            if (code == 0x1f) { command(w, K_SAVE_ALL); return; }               /* Ctrl+K S */
        }
        if (ctrl) {
            if (shift && code == 0x19) { overlay_open(w, OV_COMMANDS); return; }   /* Ctrl+Shift+P */
            if (code == 0x19) { overlay_open(w, OV_QUICK); return; }               /* Ctrl+P */
            if (shift && code == 0x21) { command(w, K_SEARCH); return; }           /* Ctrl+Shift+F */
            if (shift && code == 0x12) { command(w, K_EXPLORER); return; }         /* Ctrl+Shift+E */
            if (shift && code == 0x32) { command(w, K_PROBLEMS); return; }         /* Ctrl+Shift+M */
            if (shift && code == 0x16) { command(w, K_OUTPUT); return; }           /* Ctrl+Shift+U */
            if (shift && code == 0x1f) { command(w, K_SAVE_AS); return; }          /* Ctrl+Shift+S */
            if (shift && code == 0x25) { command(w, K_DELETE_LINE); return; }      /* Ctrl+Shift+K */
            if (shift && code == 0x20) { command(w, K_DUPLICATE); return; }        /* Ctrl+Shift+D */
            if (shift && code == 0x2c) { command(w, K_REDO); return; }             /* Ctrl+Shift+Z */
            if (code == 0x25) { chord = 1; set_status(w, "(Ctrl+K) waiting for the second key..."); return; }
            if (code == 0x29) { command(w, K_TERMINAL); return; }                  /* Ctrl+` */
            if (code == 0x30) { command(w, K_SIDEBAR); return; }                   /* Ctrl+B */
            if (code == 0x24) { command(w, K_PANEL); return; }                     /* Ctrl+J */
            if (code == 0x31) { command(w, K_NEW); return; }                       /* Ctrl+N */
            if (code == 0x18) { command(w, K_OPEN_FILE); return; }                 /* Ctrl+O */
            if (code == 0x1f) { command(w, K_SAVE); return; }                      /* Ctrl+S */
            if (code == 0x11) { command(w, K_CLOSE); return; }                     /* Ctrl+W */
            if (code == 0x21) { command(w, K_FIND); return; }                      /* Ctrl+F */
            if (code == 0x23) { command(w, K_REPLACE); return; }                   /* Ctrl+H */
            if (code == 0x22) { command(w, K_GOTO); return; }                      /* Ctrl+G */
            if (code == 0x35) { command(w, K_COMMENT); return; }                   /* Ctrl+/ */
            if (code == 0x2c) { command(w, K_UNDO); return; }                      /* Ctrl+Z */
            if (code == 0x15) { command(w, K_REDO); return; }                      /* Ctrl+Y */
            if (code == BGX_KEY_TAB || code == 0xd1 || code == 0xc9) {             /* Ctrl+Tab, Ctrl+PageDown/Up */
                int step = code == 0xc9 || shift ? DOCS - 1 : 1;
                for (int k = 1; k <= DOCS; k++) { int i = (s->active + k * step) % DOCS; if (pool.docs[i] && pool.docs[i]->used) { s->active = i; break; } }
                return;
            }
        }
        if (code == 0x3f) {                                     /* F5 debug (Python), Ctrl+F5 run */
            Doc *a = active_doc(w);
            if (!ctrl && a && a->lang == LANG_PY) debug_active(w); else command(w, K_RUN);
            return;
        }
        if (code == 0x43 && d) { toggle_break(d, gte_line_of(&d->edit, d->edit.cursor) + 1); return; }   /* F9 */
        if (code == 0x3b) { command(w, K_DOCS); return; }       /* F1 */
        if (code == 0x42 && pool.problem_count) {               /* F8: the next problem */
            static int next; next = (next + 1) % pool.problem_count;
            go_to(w, pool.problems[next].file, pool.problems[next].line, pool.problems[next].column);
            return;
        }
        if (s->focus == F_FIND || s->focus == F_REPLACE) {
            if (code == BGX_KEY_ESC) { s->find_open = 0; s->focus = F_EDITOR; return; }
            if (code == BGX_KEY_TAB && s->replace_open) { s->focus = s->focus == F_FIND ? F_REPLACE : F_FIND; return; }
            GEdit *field = s->focus == F_FIND ? &s->find : &s->replace;
            int r = g_edit_key(field, e);
            if (r == 2) { if (s->focus == F_FIND) find_next(w, shift); else replace_one(w); }
            else if (r == 1 && s->focus == F_FIND && d) { int a, b; int from = gte_selection(&d->edit, &a, &b) ? a : d->edit.cursor; int at = find_text(d, s->find.text, from, 0, s->match_case); if (at >= 0) { d->edit.anchor = at; d->edit.cursor = at + g_len(s->find.text); d->edit.shown = -1; } }
            return;
        }
        if (s->focus == F_SEARCH) {
            int r = g_edit_key(&s->search, e);
            if (r == 2) search_files(w);
            else if (code == BGX_KEY_DOWN && pool.result_selected + 1 < pool.result_count) pool.result_selected++;
            else if (code == BGX_KEY_UP && pool.result_selected > 0) pool.result_selected--;
            return;
        }
        if (s->focus == F_TREE) {
            if (code == BGX_KEY_UP && pool.tree_selected > 0) pool.tree_selected--;
            else if (code == BGX_KEY_DOWN && pool.tree_selected + 1 < pool.tree_count) pool.tree_selected++;
            else if (pool.tree_count && (code == BGX_KEY_ENTER || code == BGX_KEY_RIGHT || code == BGX_KEY_LEFT)) {
                Node *n = &pool.tree[pool.tree_selected];
                if (n->dir && (code != BGX_KEY_LEFT || n->open) && (code != BGX_KEY_RIGHT || !n->open)) tree_toggle(pool.tree_selected);
                else if (!n->dir && code == BGX_KEY_ENTER) open_file(w, n->path);
            } else if (code == 0x3c && pool.tree_count) g_prompt(w, P_RENAME, "Rename", "New name:", base_name(pool.tree[pool.tree_selected].path));   /* F2 */
            else if (code == 0xd3 && pool.tree_count) { char t[300]; g_format(t, sizeof(t), g_tr("Delete %s?"), base_name(pool.tree[pool.tree_selected].path)); g_confirm(w, C_DELETE, "Barnix Code", t); }
            else if (code == BGX_KEY_ESC) s->focus = F_EDITOR;
            return;
        }
        if (s->focus == F_TERMINAL) {
            if (code == BGX_KEY_UP || code == BGX_KEY_DOWN) {
                if (!s->history_count) return;
                s->history_at += code == BGX_KEY_UP ? -1 : 1;
                if (s->history_at < 0) s->history_at = 0;
                if (s->history_at >= s->history_count) { s->history_at = s->history_count; g_edit_set(&s->terminal, ""); return; }
                g_edit_set(&s->terminal, s->history[s->history_at]); return;
            }
            if (code == 0xc9) { pool.term_scroll += 8; if (pool.term_scroll > pool.term_count) pool.term_scroll = pool.term_count; return; }
            if (code == 0xd1) { pool.term_scroll -= 8; if (pool.term_scroll < 0) pool.term_scroll = 0; return; }
            if (code == BGX_KEY_ESC) { s->focus = F_EDITOR; return; }
            if (g_edit_key(&s->terminal, e) == 2) {
                char line[256]; g_copy(line, s->terminal.text, sizeof(line));
                if (line[0]) { if (s->history_count == 16) { for (int i = 1; i < 16; i++) g_copy(s->history[i - 1], s->history[i], 128); s->history_count--; } g_copy(s->history[s->history_count++], line, 128); }
                s->history_at = s->history_count;
                g_edit_set(&s->terminal, "");
                terminal_command(w, line);
            }
            return;
        }
        if (d) {
            if (ctrl && code == 0x20) { select_next_occurrence(d); return; }                 /* Ctrl+D */
            if (ctrl && code == 0x26) {                                                      /* Ctrl+L: the line, then the next */
                int a, b;
                if (gte_selection(&d->edit, &a, &b)) { int end = line_end_of(d, b); d->edit.cursor = end < d->edit.length ? end + 1 : end; }
                else { int end = line_end_of(d, d->edit.cursor); d->edit.anchor = line_start_of(d, d->edit.cursor); d->edit.cursor = end < d->edit.length ? end + 1 : end; }
                return;
            }
            if (ctrl && (code == 0x1b || code == 0x1a)) { snapshot(d, 1); indent_lines(d, code == 0x1a); return; }   /* Ctrl+] Ctrl+[ */
            if (code == BGX_KEY_ESC) { d->edit.anchor = -1; s->find_open = 0; return; }
            editor_key(w, d, e);
        }
        return;
    }
    if (e->type == EV_WHEEL) {
        if (e->x >= ex && e->y >= py && s->panel) {
            if (s->panel_tab == PANEL_TERMINAL) { pool.term_scroll -= e->id * 3; if (pool.term_scroll < 0) pool.term_scroll = 0; if (pool.term_scroll > pool.term_count) pool.term_scroll = pool.term_count; }
            else if (s->panel_tab == PANEL_OUTPUT) { pool.output_top += e->id * 3; if (pool.output_top < 0) pool.output_top = 0; }
            else { pool.problem_top += e->id * 2; if (pool.problem_top > pool.problem_count - 1) pool.problem_top = pool.problem_count - 1; if (pool.problem_top < 0) pool.problem_top = 0; }
            return;
        }
        if (sw && e->x >= 44 && e->x < 44 + sw) {
            if (s->side == SIDE_EXPLORER) { pool.tree_top += e->id * 3; if (pool.tree_top > pool.tree_count - 1) pool.tree_top = pool.tree_count - 1; if (pool.tree_top < 0) pool.tree_top = 0; pool.tree_selected = pool.tree_top; }
            else { pool.result_selected += e->id * 2; if (pool.result_selected >= pool.result_count) pool.result_selected = pool.result_count - 1; if (pool.result_selected < 0) pool.result_selected = 0; }
            return;
        }
        if (d) gte_mouse(&d->edit, e, ex, ey, ew - (ew >= 500 ? 70 : 0), eh);
        return;
    }
    if (e->type == EV_MOUSE_DOWN || e->type == EV_DOUBLE) {
        int x = e->x, y = e->y, dbl = e->type == EV_DOUBLE;
        if (s->overlay) {   /* a click in the list picks; outside closes */
            int ow = ew - 80 < 600 ? ew - 80 : 600, ox = ex + (ew - ow) / 2;
            int row = (y - 40) / ROW;
            if (x >= ox && x < ox + ow && y >= 40 && row >= 0 && s->overlay_top + row < s->overlay_count) { s->overlay_selected = s->overlay_top + row; overlay_accept(w); }
            else if (!(x >= ox && x < ox + ow && y < 40)) s->overlay = OV_NONE;
            return;
        }
        if (x < 44 && y < w->h - 22) {   /* activity bar */
            int i = (y - 4) / 48;
            if (i >= 0 && i < 3) { if (s->sidebar && s->side == i) s->sidebar = 0; else { s->sidebar = 1; s->side = i; s->focus = i == SIDE_SEARCH ? F_SEARCH : F_TREE; } }
            save_session(w);
            return;
        }
        if (sw && x >= 44 && x < 44 + sw && y < w->h - 22) {   /* side bar */
            if (s->side == SIDE_EXPLORER) {
                if (y >= 28 && y < 50) {
                    if (x >= 44 + sw - 50 && x < 44 + sw - 36) command(w, K_NEW_FILE);
                    else if (x >= 44 + sw - 34) command(w, K_NEW_FOLDER);
                    return;
                }
                int i = pool.tree_top + (y - 54) / ROW;
                s->focus = F_TREE;
                if (y >= 54 && i < pool.tree_count) {
                    pool.tree_selected = i;
                    if (pool.tree[i].dir) tree_toggle(i); else open_file(w, pool.tree[i].path);
                }
            } else if (s->side == SIDE_SEARCH) {
                if (y < 56) s->focus = F_SEARCH;
                else if (y < 76) s->search_case = !s->search_case;
                else {
                    int i = pool.result_top + (y - 80) / 34;
                    if (i < pool.result_count) {
                        pool.result_selected = i;
                        char path[300]; file_path(w, pool.results[i].file, path, sizeof(path));
                        if (open_file(w, path) >= 0) { Doc *o = active_doc(w); gte_goto_line(&o->edit, pool.results[i].line - 1); o->edit.shown = -1; select_word(o); }
                    }
                }
            } else if (y >= 230 && y < 258) command(w, K_RUN);
            return;
        }
        if (y < 30 && x >= ex) {   /* tabs */
            for (int i = 0; i < DOCS; i++)
                if (s->tab_w[i] && x >= s->tab_x[i] && x < s->tab_x[i] + s->tab_w[i]) {
                    if (x >= s->tab_x[i] + s->tab_w[i] - 26) close_doc(w, i, 0);
                    else { s->active = i; s->focus = F_EDITOR; }
                    return;
                }
            return;
        }
        if (s->panel && y >= py && y < w->h - 22 && x >= ex) {   /* bottom panel */
            if (y < py + 6) { s->dragging_panel = 1; return; }
            if (y < py + 24) {
                if (x >= ex + ew - 26) { s->panel = 0; return; }
                int tx = ex + 10;
                static const char *const tabs[] = {"PROBLEMS", "OUTPUT", "TERMINAL"};
                for (int i = 0; i < 3; i++) {
                    int width = bgx_text_width(tabs[i], 1) + 16 + (i == PANEL_PROBLEMS && pool.problem_count ? 18 : 0);
                    if (x >= tx && x < tx + width + 8) { s->panel_tab = i; if (i == PANEL_TERMINAL) s->focus = F_TERMINAL; }
                    tx += width + 8;
                }
                return;
            }
            if (s->panel_tab == PANEL_TERMINAL) s->focus = F_TERMINAL;
            else if (s->panel_tab == PANEL_PROBLEMS) {
                int i = pool.problem_top + (y - py - 26) / 18;
                if (i >= 0 && i < pool.problem_count) go_to(w, pool.problems[i].file, pool.problems[i].line, pool.problems[i].column);
            }
            return;
        }
        if (s->find_open && d) {   /* the find widget */
            int fw = 360, fx = ex + ew - fw - 84, fy = ey + 2, fh = s->replace_open ? 58 : 32;
            if (x >= fx && x < fx + fw && y >= fy && y < fy + fh) {
                if (y < fy + 30) {
                    if (x >= fx + 326) { s->find_open = 0; s->focus = F_EDITOR; }
                    else if (x >= fx + 300) s->match_case = !s->match_case;
                    else s->focus = F_FIND;
                } else {
                    if (x >= fx + 340) replace_all(w);
                    else if (x < fx + 210) s->focus = F_REPLACE;
                    else replace_one(w);
                }
                return;
            }
        }
        if (d && x >= ex && y >= ey && y < ey + eh) {
            int mini = ew >= 500 ? 70 : 0;
            if (mini && x >= ex + ew - mini) {   /* minimap: jump there */
                int total = gte_line_of(&d->edit, d->edit.length) + 1, scale = total * 2 > eh ? (total * 2 + eh - 1) / eh : 1;
                int line = (y - ey) / 2 * scale;
                gte_goto_line(&d->edit, line); d->edit.shown = -1;
                return;
            }
            s->focus = F_EDITOR;
            complete_close();
            if (!dbl && x < ex + gte_gutter_width(&d->edit) && e->button == 1) {   /* the gutter: a breakpoint */
                int line = d->edit.top + (y - ey - 2) / 16 + 1;
                if (line <= gte_line_of(&d->edit, d->edit.length) + 1) toggle_break(d, line);
                return;
            }
            gte_mouse(&d->edit, e, ex, ey, ew - mini, eh);
        }
        return;
    }
    if (e->type == EV_MOUSE_MOVE) {
        if (s->dragging_panel) {
            int h = w->h - 22 - e->y;
            if (h < 80) h = 80;
            if (h > w->h - 160) h = w->h - 160;
            s->panel_h = h;
            return;
        }
        if (d) gte_mouse(&d->edit, e, ex, ey, ew - (ew >= 500 ? 70 : 0), eh);
        return;
    }
    if (e->type == EV_MOUSE_UP) {
        if (s->dragging_panel) { s->dragging_panel = 0; save_session(w); return; }
        if (d) gte_mouse(&d->edit, e, ex, ey, ew, eh);
        return;
    }
    if (e->type == EV_PROMPT && e->text[0]) {
        char path[300];
        if (e->id == P_SAVE_AS) {
            if (d) { g_copy(d->path, e->text, sizeof(d->path)); d->untitled = 0; if (!save_doc(w, d)) { pool.files_ready = 0; save_session(w); } }
        } else if (e->id == P_SAVE_AS + 50) {
            unsigned int position = 0; BarnixDirEntry entry;
            if (barnix->directory(e->text, &position, &entry) > 0 && barnix->size(e->text) < 0) open_folder(w, e->text);
            else open_file(w, e->text);
        } else if (e->id == P_FOLDER) open_folder(w, e->text);
        else if (e->id == P_NEW_FILE || e->id == P_NEW_FOLDER) {
            char dir[256];
            g_copy(dir, s->folder, sizeof(dir));
            if (pool.tree_count && pool.tree_selected < pool.tree_count) {
                Node *n = &pool.tree[pool.tree_selected];
                if (n->dir) g_copy(dir, n->path, sizeof(dir));
                else { g_copy(dir, n->path, sizeof(dir)); for (int i = g_len(dir) - 1; i > 0; i--) if (dir[i] == '/') { dir[i] = 0; break; } }
            }
            g_path_join(path, sizeof(path), dir, e->text);
            if (e->id == P_NEW_FOLDER) { if (barnix->mkdir(path)) g_error(w, "Barnix Code", "Cannot make the folder."); }
            else { if (barnix->size(path) < 0 && g_write_file(path, "", 0)) g_error(w, "Barnix Code", "Cannot make the file."); else open_file(w, path); }
            tree_load(w);
        } else if (e->id == P_RENAME && pool.tree_count) {
            Node *n = &pool.tree[pool.tree_selected];
            char dir[256]; g_copy(dir, n->path, sizeof(dir));
            for (int i = g_len(dir) - 1; i > 0; i--) if (dir[i] == '/') { dir[i] = 0; break; }
            g_path_join(path, sizeof(path), dir, e->text);
            if (barnix->mv(n->path, path)) g_error(w, "Barnix Code", "Cannot rename it.");
            else { for (int i = 0; i < DOCS; i++) if (pool.docs[i] && pool.docs[i]->used && g_equal(pool.docs[i]->path, n->path)) g_copy(pool.docs[i]->path, path, sizeof(pool.docs[i]->path)); tree_load(w); }
        }
        return;
    }
    if (e->type == EV_CONFIRM) {
        if (e->id >= C_CLOSE && e->id < C_CLOSE + DOCS) close_doc(w, e->id - C_CLOSE, 1);
        else if (e->id == C_DELETE && pool.tree_count) {
            Node *n = &pool.tree[pool.tree_selected];
            if (n->dir ? barnix->rmdir(n->path) : barnix->rm(n->path)) g_error(w, "Barnix Code", "Cannot delete it (a folder must be empty).");
            tree_load(w);
        }
    }
}
/* Like VS Code's hot exit: open files, saved or not, wait in memory for the
 * next window (Gouse keeps running); the session file has the saved ones. */
static void close_window(GWindow *w) { save_session(w); }
const GApp app_code = {"Barnix Code", "A code editor like VS Code: C, C++, Python, web", ICON_CODE, 1000, 680, 640, 420,
                       APP_RESIZABLE | APP_SINGLE, 0, 0, sizeof(State), open, event, 0, paint, close_window};
