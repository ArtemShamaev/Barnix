/* Happy Gouse window manager: desktop, Gouse menu, taskbar, windows, menus,
 * dialogs and the cooperative scheduler that runs every window each frame. */
#include "gouse.h"

extern const GApp app_executive, app_notepad, app_terminal, app_barnocraft, app_paint, app_calculator,
    app_clock, app_calendar, app_cardfile, app_reversi, app_mines, app_puzzle, app_control,
    app_clipboard, app_about, app_piano, app_media, app_browser, app_guide, app_code, app_store, app_alpha;
const GApp *const g_apps[] = {&app_executive, &app_browser, &app_store, &app_notepad, &app_terminal, &app_code, &app_alpha, &app_barnocraft, &app_paint,
    &app_piano, &app_media, &app_calculator, &app_clock, &app_calendar, &app_cardfile, &app_reversi,
    &app_mines, &app_puzzle, &app_control, &app_guide, &app_clipboard, &app_about};
const int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);

/* ---------------------------------------------------------------- themes and settings */
const GTheme g_themes[] = {
    {"Gouse Blue", 0x1f4fa8, 0x4a8ee6, 0x8a94a6, 0xffffff, 0xc8ccd4, 0xffffff, 0x8a909c, 0x2a2e36, 0xffffff, 0x101418, 0x2f6fd0, 0xffffff, 0xe8eaee},
    {"Meadow", 0x2e7d32, 0x6abf69, 0x8f9a8f, 0xffffff, 0xc9d3c5, 0xffffff, 0x88947f, 0x283024, 0xffffff, 0x10180f, 0x3c8f40, 0xffffff, 0xe8eee4},
    {"Plum", 0x5e1d82, 0xa64fc9, 0x9a8ea4, 0xffffff, 0xd2c8d8, 0xffffff, 0x958a9c, 0x2e2434, 0xffffff, 0x1a1020, 0x8a3fb0, 0xffffff, 0xeee8f2},
    {"Graphite", 0x2f3a42, 0x6f8592, 0x9aa0a6, 0xffffff, 0xb6bcc2, 0xf4f6f8, 0x7c838a, 0x22262a, 0xf8f9fa, 0x101214, 0x46606e, 0xffffff, 0xdfe3e7},
    {"Retro 1985", 0x000080, 0x1084d0, 0x808080, 0xffffff, 0xc0c0c0, 0xffffff, 0x808080, 0x000000, 0xffffff, 0x000000, 0x000080, 0xffffff, 0xc0c0c0},
    {"Sunset", 0xa8321f, 0xf09a3e, 0xa6968a, 0xffffff, 0xd8ccc0, 0xffffff, 0x9c8e80, 0x342a22, 0xfffdf8, 0x1c140e, 0xd0602f, 0xffffff, 0xf2eae0},
};
const int g_theme_count = sizeof(g_themes) / sizeof(g_themes[0]);
const unsigned int g_desktop_colors[] = {0x2a8c8c, 0x3a6fb8, 0x2e7d4f, 0x5e45a8, 0x607080, 0x1e2430, 0xb34766, 0xb89a5e};
const int g_desktop_color_count = sizeof(g_desktop_colors) / sizeof(g_desktop_colors[0]);
const char *const g_pattern_names[] = {"None", "Dots", "Checks", "Bricks", "Stripes", "Gouse"};
const int g_pattern_count = 6;
GSettings g_settings = {0, 0, 5, 3, 1, 0, 4, 1, -1, 0, 0};
GTheme g_theme;

/* ---------------------------------------------------------------- state */
int g_screen_w, g_screen_h;
unsigned char g_held[256], g_pressed[256], g_typed[256];
char g_clipboard[16384];
int g_clipboard_len;
int g_look_dx, g_look_dy;
static unsigned int *screen_pixels, *background;
static BgxCanvas screen;
static GWindow windows[MAX_WINDOWS];
static unsigned int state_capacity[MAX_WINDOWS];
static int order[MAX_WINDOWS], order_count;          /* z-order, last = top */
static GWindow *capture_window;
static int running = 1, redraw = 1, background_ready;
static int mouse_x, mouse_y, mouse_rx, mouse_ry;      /* cursor and sub-pixel remainder */
static unsigned int buttons;
static int start_open, start_hover = -1, selected_icon = -1;
static int menu_window = -1, menu_open = -1, menu_hover = -1;
static enum { DRAG_NONE, DRAG_MOVE, DRAG_SIZE, DRAG_CLIENT, DRAG_CHROME } drag;
static GWindow *drag_window;
static int drag_dx, drag_dy;
static unsigned int drag_button;
static unsigned int last_click_time;
static int last_click_x, last_click_y;
static unsigned char *memory_base;
static unsigned int memory_size, memory_used;
static char launch_path[256];
static int icon_count(void);
static int draw_frame(void);
static int painting;

/* ---------------------------------------------------------------- memory */
void *g_alloc(unsigned int bytes)
{
    bytes = (bytes + 15) & ~15U;
    if (!memory_base || bytes > memory_size - memory_used) return 0;
    void *block = memory_base + memory_used;
    memory_used += bytes;
    unsigned int *p = block;
    for (unsigned int i = 0; i < bytes / 4; i++) p[i] = 0;
    return block;
}
unsigned int g_memory_free(void) { return memory_size - memory_used; }
unsigned int g_memory_total(void) { return memory_size; }
unsigned int g_now(void) { return bgx_millis(); }
/* Live state: a modifier and its key often arrive in the same frame. */
int g_shift(void) { return bgx_key_held(0x2a) || bgx_key_held(0x36); }
int g_ctrl(void) { return bgx_key_held(0x1d) || bgx_key_held(0x9d); }
static int alt(void) { return bgx_key_held(0x38) || bgx_key_held(0xb8); }
int g_alt(void) { return alt(); }
static void copy_pixels(unsigned int *out, const unsigned int *in, int count) { for (int i = 0; i < count; i++) out[i] = in[i]; }

/* g_clipboard mirrors the system clipboard (ABI 14), which the shell,
 * macro, programs run from the Prompt and every other program share. */
static unsigned int clipboard_seen;
void g_clipboard_set(const char *text, int length)
{
    if (length > (int)sizeof(g_clipboard) - 1) length = sizeof(g_clipboard) - 1;
    for (int i = 0; i < length; i++) g_clipboard[i] = text[i];
    g_clipboard[length] = 0; g_clipboard_len = length;
    if (barnix->version >= 14) { barnix->clipboard_set(g_clipboard, (unsigned int)length); clipboard_seen = barnix->clipboard_serial(); }
    GWindow *viewer = g_find(&app_clipboard);
    if (viewer) g_invalidate(viewer);
}
void g_clipboard_sync(void)
{
    if (barnix->version < 14 || barnix->clipboard_serial() == clipboard_seen) return;
    clipboard_seen = barnix->clipboard_serial();
    int n = barnix->clipboard_get(g_clipboard, sizeof(g_clipboard));
    g_clipboard_len = n < (int)sizeof(g_clipboard) ? n : (int)sizeof(g_clipboard) - 1;
    GWindow *viewer = g_find(&app_clipboard);
    if (viewer) g_invalidate(viewer);
}

/* ---------------------------------------------------------------- settings */
static void settings_path(char *out, int capacity) { g_path_join(out, capacity, g_home(), ".gouse.cfg"); }
void g_settings_apply(void)
{
    if (g_settings.theme < 0 || g_settings.theme >= g_theme_count) g_settings.theme = 0;
    if (g_settings.desktop_color < 0 || g_settings.desktop_color >= g_desktop_color_count) g_settings.desktop_color = 0;
    if (g_settings.pattern < 0 || g_settings.pattern >= g_pattern_count) g_settings.pattern = 0;
    if (g_settings.mouse_speed < 1 || g_settings.mouse_speed > 5) g_settings.mouse_speed = 3;
    if (g_settings.volume < 0 || g_settings.volume > 4) g_settings.volume = 4;
    g_settings.sounds = g_settings.sounds != 0;
    if (g_settings.lang < -1 || g_settings.lang > 1) g_settings.lang = -1;
    /* Russian unless English was chosen in the Control Panel. */
    g_russian = g_settings.lang != 0;
    bsnd_volume(g_settings.volume * 64);
    g_theme = g_themes[g_settings.theme];
    background_ready = 0; redraw = 1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].used) windows[i].dirty = 1;
}
void g_settings_save(void)
{
    char path[160], text[192];
    settings_path(path, sizeof(path));
    int n = g_format(text, sizeof(text), "theme=%d\ndesktop=%d\npattern=%d\nmouse=%d\nclock24=%d\nseconds=%d\nvolume=%d\nsounds=%d\n",
                     g_settings.theme, g_settings.desktop_color, g_settings.pattern, g_settings.mouse_speed,
                     g_settings.clock_24h, g_settings.clock_seconds, g_settings.volume, g_settings.sounds);
    if (g_settings.lang >= 0) n += g_format(text + n, sizeof(text) - n, "lang=%d\n", g_settings.lang);
    if (g_settings.screen_w) n += g_format(text + n, sizeof(text) - n, "screen=%dx%d\n", g_settings.screen_w, g_settings.screen_h);
    g_write_file(path, text, n);
}
static void settings_load(void)
{
    char path[160], text[256];
    settings_path(path, sizeof(path));
    int n = g_read_file(path, text, sizeof(text) - 1);
    if (n > 0) {
        text[n] = 0;
        for (char *line = text; *line;) {
            char *end = line; while (*end && *end != '\n') end++;
            char saved = *end; *end = 0;
            char *eq = line; while (*eq && *eq != '=') eq++;
            if (*eq) {
                *eq = 0; int v = g_atoi(eq + 1);
                if (g_equal(line, "theme")) g_settings.theme = v;
                else if (g_equal(line, "desktop")) g_settings.desktop_color = v;
                else if (g_equal(line, "pattern")) g_settings.pattern = v;
                else if (g_equal(line, "mouse")) g_settings.mouse_speed = v;
                else if (g_equal(line, "clock24")) g_settings.clock_24h = v;
                else if (g_equal(line, "seconds")) g_settings.clock_seconds = v;
                else if (g_equal(line, "volume")) g_settings.volume = v;
                else if (g_equal(line, "sounds")) g_settings.sounds = v;
                else if (g_equal(line, "lang")) g_settings.lang = v;
                else if (g_equal(line, "screen")) {
                    const char *x = eq + 1; while (*x && *x != 'x') x++;
                    g_settings.screen_w = v; g_settings.screen_h = *x ? g_atoi(x + 1) : 0;
                }
            }
            *end = saved; line = *end ? end + 1 : end;
        }
    }
    g_settings_apply();
}

/* ---------------------------------------------------------------- windows */
static int index_of(const GWindow *w) { return (int)(w - windows); }
static GWindow *top_window(void) { return order_count ? &windows[order[order_count - 1]] : 0; }
int g_focused(const GWindow *w) { return w && top_window() == w && !w->minimized; }
void g_invalidate(GWindow *w) { if (w) { w->dirty = 1; redraw = 1; } }
GWindow *g_find(const GApp *app)
{
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].used && windows[i].app == app) return &windows[i];
    return 0;
}
const GApp *g_app_named(const char *name)
{
    for (int i = 0; i < g_app_count; i++) if (g_equal_ci(g_apps[i]->name, name)) return g_apps[i];
    return 0;
}
void g_set_title(GWindow *w, const char *title) { g_copy(w->title, title, sizeof(w->title)); redraw = 1; }
static int system_event(GWindow *w, GEvent *e);
static void send(GWindow *w, GEvent *e)
{
    if (system_event(w, e)) return;
    if (w && w->app->event) w->app->event(w, e);
    g_invalidate(w);
}
static void send_simple(GWindow *w, int type) { GEvent e = {0}; e.type = type; send(w, &e); }
void g_capture(GWindow *w, int on)
{
    if (on && g_focused(w)) capture_window = w;
    else if (capture_window == w) capture_window = 0;
}
int g_captured(const GWindow *w) { return w && capture_window == w; }
static int menu_height(const GWindow *w) { return w->app->menu_count && !w->fullscreen ? MENU_H : 0; }
static int chrome(const GWindow *w) { return !w->fullscreen; }
/* Outer frame of a window. */
static void outer(const GWindow *w, int *x, int *y, int *width, int *height)
{
    if (!chrome(w)) { *x = w->x; *y = w->y; *width = w->w; *height = w->h; return; }
    *x = w->x - BORDER; *y = w->y - BORDER - TITLE_H - menu_height(w);
    *width = w->w + 2 * BORDER; *height = w->h + 2 * BORDER + TITLE_H + menu_height(w);
}
static void resize(GWindow *w, int width, int height)
{
    if (width < w->app->min_w) width = w->app->min_w;
    if (height < w->app->min_h) height = w->app->min_h;
    if (width > g_screen_w) width = g_screen_w;
    if (height > g_screen_h) height = g_screen_h;
    if (width == w->w && height == w->h) return;
    w->w = width; w->h = height;
    w->canvas.width = width; w->canvas.height = height;
    send_simple(w, EV_RESIZE);
}
/* Grows or shrinks a window and keeps it on the screen. */
void g_resize(GWindow *w, int width, int height)
{
    if (w->maximized || w->fullscreen) return;
    resize(w, width, height);
    int x, y, ow, oh;
    outer(w, &x, &y, &ow, &oh);
    if (x + ow > g_screen_w) w->x -= x + ow - g_screen_w;
    if (y + oh > g_screen_h - TASKBAR_H) w->y -= y + oh - (g_screen_h - TASKBAR_H);
    outer(w, &x, &y, &ow, &oh);
    if (x < 0) w->x -= x;
    if (y < 0) w->y -= y;
    g_invalidate(w); redraw = 1;
}
static void blur(GWindow *w)
{
    if (!w) return;
    if (capture_window == w) capture_window = 0;
    send_simple(w, EV_BLUR);
}
void g_focus(GWindow *w)
{
    if (!w || !w->used) return;
    GWindow *previous = top_window();
    int at = 0;
    while (at < order_count && order[at] != index_of(w)) at++;
    if (at == order_count) return;
    for (int i = at; i < order_count - 1; i++) order[i] = order[i + 1];
    order[order_count - 1] = index_of(w);
    w->minimized = 0;
    if (previous != w) { blur(previous); send_simple(w, EV_FOCUS); }
    menu_open = -1; redraw = 1;
}
static int quiet;   /* restoring the session: windows open without a sound */
GWindow *g_open(const GApp *app, const char *argument)
{
    if (app->flags & APP_SINGLE) {
        GWindow *existing = g_find(app);
        if (existing) { g_focus(existing); return existing; }
    }
    int slot = 0;
    while (slot < MAX_WINDOWS && windows[slot].used) slot++;
    if (slot == MAX_WINDOWS) { g_error(top_window(), "Happy Gouse", "Too many windows are open."); return 0; }
    GWindow *w = &windows[slot];
    if (!w->pixels) w->pixels = g_alloc((unsigned int)g_screen_w * g_screen_h * 4);
    if (app->state_size && state_capacity[slot] < (unsigned int)app->state_size) {
        void *state = g_alloc(app->state_size);
        if (state) { w->state = state; state_capacity[slot] = app->state_size; }
        else state_capacity[slot] = 0;
    }
    if (!w->pixels || (app->state_size && state_capacity[slot] < (unsigned int)app->state_size)) {
        g_error(top_window(), "Happy Gouse", "Not enough memory for another window.");
        return 0;
    }
    if (app->state_size) { unsigned int *p = w->state; for (int i = 0; i < app->state_size / 4; i++) p[i] = 0; }
    void *state = w->state; unsigned int *pixels = w->pixels;
    unsigned char *raw = (unsigned char *)w;
    for (unsigned int i = 0; i < sizeof(*w); i++) raw[i] = 0;
    w->state = state; w->pixels = pixels; w->used = 1; w->app = app; w->pressed_zone = -1;
    g_copy(w->title, app->name, sizeof(w->title));
    int width = app->w, height = app->h, desk_h = g_screen_h - TASKBAR_H;
    if (width > g_screen_w - 2 * BORDER) width = g_screen_w - 2 * BORDER;
    if (height > desk_h - 2 * BORDER - TITLE_H - MENU_H) height = desk_h - 2 * BORDER - TITLE_H - MENU_H;
    w->w = width; w->h = height;
    w->canvas.pixels = pixels; w->canvas.width = width; w->canvas.height = height;
    int n = order_count % 8;
    int rows = (g_screen_h - TASKBAR_H - 8) / 76, icon_columns = (icon_count() + (rows > 0 ? rows : 1) - 1) / (rows > 0 ? rows : 1);
    int left = 8 + icon_columns * 86 + BORDER + 6;   /* right of the desktop icons when there is room */
    if (left + width + BORDER > g_screen_w) left = 48;
    w->x = left + n * 26;
    w->y = 26 + n * 22 + TITLE_H + menu_height(w);
    if (w->x + w->w + BORDER > g_screen_w) w->x = g_screen_w - w->w - BORDER;
    if (w->y + w->h + BORDER > desk_h) w->y = desk_h - w->h - BORDER;
    if (w->x < BORDER) w->x = BORDER;
    if (w->y < BORDER + TITLE_H + menu_height(w)) w->y = BORDER + TITLE_H + menu_height(w);
    order[order_count++] = slot;
    GWindow *previous = order_count > 1 ? &windows[order[order_count - 2]] : 0;
    blur(previous);
    w->dirty = 1; redraw = 1;
    if (!quiet) g_sound(G_SOUND_OPEN);
    if (app->open) app->open(w, argument);
    return w->used ? w : 0;
}
void g_close(GWindow *w)
{
    if (w && w->busy) { g_message_sound(w, "Happy Gouse", "Wait a moment: this window is busy.", -1); return; }
    if (!w || !w->used) return;
    if (w->app->close) w->app->close(w);
    g_sound(G_SOUND_CLOSE);
    if (capture_window == w) capture_window = 0;
    if (drag_window == w) { drag = DRAG_NONE; drag_window = 0; }
    int at = 0;
    while (at < order_count && order[at] != index_of(w)) at++;
    for (int i = at; i < order_count - 1; i++) order[i] = order[i + 1];
    if (at < order_count) order_count--;
    w->used = 0; menu_open = -1; redraw = 1;
    GWindow *top = top_window();
    if (top && !top->minimized) send_simple(top, EV_FOCUS);
}
static void maximize(GWindow *w, int on)
{
    if (w->fullscreen) return;
    if (on != w->maximized && !quiet) g_sound(on ? G_SOUND_MAXIMIZE : G_SOUND_MINIMIZE);
    if (on && !w->maximized) {
        w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h; w->maximized = 1;
        w->x = BORDER; w->y = BORDER + TITLE_H + menu_height(w);
        resize(w, g_screen_w - 2 * BORDER, g_screen_h - TASKBAR_H - 2 * BORDER - TITLE_H - menu_height(w));
    } else if (!on && w->maximized) {
        w->maximized = 0; w->x = w->saved_x; w->y = w->saved_y;
        resize(w, w->saved_w, w->saved_h);
    }
    g_invalidate(w);
}
void g_toggle_fullscreen(GWindow *w)
{
    if (!w) return;
    if (!w->fullscreen) {
        if (!w->maximized) { w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h; }
        w->fullscreen = 1; w->x = 0; w->y = 0;
        int min_w = w->app->min_w, min_h = w->app->min_h;
        resize(w, g_screen_w > min_w ? g_screen_w : min_w, g_screen_h > min_h ? g_screen_h : min_h);
    } else {
        w->fullscreen = 0;
        if (w->maximized) { w->maximized = 0; quiet++; maximize(w, 1); quiet--; }
        else { w->x = w->saved_x; w->y = w->saved_y; resize(w, w->saved_w, w->saved_h); }
    }
    background_ready = 0;
    g_invalidate(w);
}
static void minimize(GWindow *w)
{
    if (!quiet) g_sound(G_SOUND_MINIMIZE);
    w->minimized = 1;
    blur(w);
    /* move it under the others so Alt+Tab and focus skip it */
    int at = 0;
    while (at < order_count && order[at] != index_of(w)) at++;
    for (int i = at; i > 0; i--) order[i] = order[i - 1];
    order[0] = index_of(w);
    GWindow *top = top_window();
    if (top && !top->minimized) send_simple(top, EV_FOCUS);
    redraw = 1;
}

/* ---------------------------------------------------------------- dialogs */
static void dialog(GWindow *w, int kind, int id, const char *title, const char *text)
{
    if (!w) w = top_window();
    if (!w) return;
    w->dialog.kind = kind; w->dialog.id = id; w->dialog.pressed = 0;
    g_copy(w->dialog.title, g_tr(title), sizeof(w->dialog.title));
    g_copy(w->dialog.text, g_tr(text), sizeof(w->dialog.text));
    g_focus(w);
    g_invalidate(w);
    if (kind == 1 && id != 1) g_sound(id ? id - 2 : G_SOUND_DING);   /* id: 0 ding, 1 silent, else sound + 2 */
    else if (kind == 3) g_sound(G_SOUND_QUESTION);
}
void g_message(GWindow *w, const char *title, const char *text) { dialog(w, 1, 0, title, text); }
void g_error(GWindow *w, const char *title, const char *text) { dialog(w, 1, G_SOUND_ERROR + 2, title, text); }
void g_message_sound(GWindow *w, const char *title, const char *text, int sound) { dialog(w, 1, sound < 0 ? 1 : sound + 2, title, text); }
void g_confirm(GWindow *w, int id, const char *title, const char *text) { dialog(w, 3, id, title, text); }
void g_prompt(GWindow *w, int id, const char *title, const char *label, const char *initial)
{
    dialog(w, 2, id, title, label);
    if (!w) w = top_window();
    if (w) { g_copy(w->dialog.edit.text, initial ? initial : "", sizeof(w->dialog.edit.text)); w->dialog.edit.cursor = g_len(w->dialog.edit.text); }
}
static void dialog_rect(const GWindow *w, int *x, int *y, int *width, int *height)
{
    int lines = 1, column = 0, widest = 0;
    for (const char *p = w->dialog.text; *p; p++) {
        if (*p == '\n') { lines++; column = 0; continue; }
        if (((unsigned char)*p & 0xc0) != 0x80 && ++column > widest) widest = column;
    }
    *width = widest * 8 + 80;
    if (*width < 340) *width = 340;
    if (*width > g_screen_w - 16) *width = g_screen_w - 16;
    *height = w->dialog.kind == 2 ? 132 : 112;
    *height += (lines - 1) * 16;
    int ox, oy, ow, oh;
    outer(w, &ox, &oy, &ow, &oh);
    *x = ox + (ow - *width) / 2; *y = oy + (oh - *height) / 2;
    if (*x < 4) *x = 4;
    if (*y < 4) *y = 4;
    if (*x + *width > g_screen_w) *x = g_screen_w - *width;
}
static void dialog_buttons(const GWindow *w, int *ok_x, int *cancel_x, int *by)
{
    int x, y, width, height;
    dialog_rect(w, &x, &y, &width, &height);
    *by = y + height - 34;
    if (w->dialog.kind == 1) { *ok_x = x + (width - 88) / 2; *cancel_x = -1000; }
    else { *ok_x = x + width / 2 - 96; *cancel_x = x + width / 2 + 8; }
}
static void dialog_finish(GWindow *w, int accepted)
{
    GDialog d = w->dialog;
    w->dialog.kind = 0;
    g_invalidate(w);
    if (!accepted || d.kind == 1) return;
    GEvent e = {0};
    e.type = d.kind == 2 ? EV_PROMPT : EV_CONFIRM; e.id = d.id; e.text = d.edit.text;
    send(w, &e);
}
static void draw_dialog(GWindow *w)
{
    int x, y, width, height, ok_x, cancel_x, by;
    dialog_rect(w, &x, &y, &width, &height);
    dialog_buttons(w, &ok_x, &cancel_x, &by);
    bgx_blend(&screen, x + 6, y + 6, width, height, 0, 90);
    g_panel(&screen, x, y, width, height, g_theme.face, 0);
    bgx_fill(&screen, x + 3, y + 3, width - 6, 20, g_theme.title_active);
    g_text_raw(&screen, x + 8, y + 5, w->dialog.title, g_theme.title_text);
    int icon = w->dialog.kind == 3 ? ICON_ABOUT : w->dialog.kind == 2 ? ICON_NOTEPAD : ICON_ABOUT;
    g_icon(&screen, x + 12, y + 32, icon, 32);
    int line_y = y + 32;
    char line[96]; const char *p = w->dialog.text;
    while (*p) {
        int n = 0;
        while (*p && *p != '\n' && n < 95) line[n++] = *p++;
        line[n] = 0;
        if (*p == '\n') p++;
        g_text_fit_raw(&screen, x + 54, line_y, width - 64, line, g_theme.text);
        line_y += 16;
    }
    if (w->dialog.kind == 2) g_edit_draw(&screen, x + 54, line_y + 4, width - 66, (const GEdit *)&w->dialog.edit, 1);
    const char *ok = w->dialog.kind == 3 ? "Yes" : "OK";
    g_panel(&screen, ok_x, by, 88, 26, g_theme.face, w->dialog.pressed == 1);
    g_text_center(&screen, ok_x, by + 5, 88, ok, g_theme.text);
    bgx_frame(&screen, ok_x - 2, by - 2, 92, 30, 1, g_theme.dark);
    if (w->dialog.kind != 1) {
        g_panel(&screen, cancel_x, by, 88, 26, g_theme.face, w->dialog.pressed == 2);
        g_text_center(&screen, cancel_x, by + 5, 88, w->dialog.kind == 3 ? "No" : "Cancel", g_theme.text);
    }
}

/* ---------------------------------------------------------------- programs and session */
static void session_path(char *out, int capacity) { g_path_join(out, capacity, g_home(), ".gouse.session"); }
static void session_save(void)
{
    char text[1024], path[160]; int n = 0;
    for (int i = 0; i < order_count; i++) {
        GWindow *w = &windows[order[i]];
        if (w->app == &app_barnocraft) continue;   /* the world is not saved */
        n += g_format(text + n, sizeof(text) - n, "%s %d %d %d %d %d\n", w->app->name, w->x, w->y, w->w, w->h, w->minimized);
        if (n >= (int)sizeof(text) - 1) break;
    }
    session_path(path, sizeof(path));
    g_write_file(path, text, n);
}
static int session_restore(void)
{
    char text[1024], path[160];
    session_path(path, sizeof(path));
    int n = g_read_file(path, text, sizeof(text) - 1);
    if (n < 0) return 0;
    barnix->rm(path);
    text[n] = 0;
    int opened = 0;
    for (char *line = text; *line;) {
        char *end = line; while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        /* app names may contain spaces: match the longest known prefix */
        const GApp *app = 0; int skip = 0;
        for (int i = 0; i < g_app_count; i++) {
            int len = g_len(g_apps[i]->name), same = 1;
            for (int j = 0; j < len; j++) if (line[j] != g_apps[i]->name[j]) same = 0;
            if (same && line[len] == ' ' && len > skip) { app = g_apps[i]; skip = len; }
        }
        if (app) {
            int v[5] = {0}, at = skip, count = 0;
            while (line[at] && count < 5) {
                while (line[at] == ' ') at++;
                v[count++] = g_atoi(line + at);
                while (line[at] && line[at] != ' ') at++;
            }
            GWindow *w = g_open(app, 0);
            if (w && count >= 4) { w->x = v[0]; w->y = v[1]; resize(w, v[2], v[3]); if (v[4]) minimize(w); opened++; }
        }
        *end = saved; line = *end ? end + 1 : end;
    }
    return opened;
}
#define CONFIRM_RUN 0x7f00
static GWindow *confirm_owner;
/* Programs run inside a Gouse Prompt window (ABI 14); older kernels and
 * the Prompt's "full" command hand the whole screen over instead. */
void g_run_program(GWindow *from, const char *path)
{
    if (barnix->version >= 14 && barnix->run && g_open(&app_terminal, path)) return;   /* "program arguments" */
    g_run_full(from, path);
}
void g_run_full(GWindow *from, const char *path)
{
    g_copy(launch_path, path, sizeof(launch_path));
    char text[300];
    g_format(text, sizeof(text), "Run %s full screen?\nHappy Gouse closes meanwhile and\ncomes back when the program ends.", path);
    confirm_owner = from ? from : top_window();
    if (!confirm_owner) { confirm_owner = g_open(&app_executive, 0); }
    if (confirm_owner) g_confirm(confirm_owner, CONFIRM_RUN, "Run program", text);
}
static int launch_requested;
void g_quit(void) { running = 0; }
/* "program arguments" gets the whole screen: Happy Gouse closes, the kernel
 * runs it and then starts Gouse again (windows come back). 0 when accepted. */
int g_launch(const char *command_line)
{
    if (barnix->launch(command_line)) return -1;
    launch_requested = 1;
    return 0;
}
void g_restart(void)
{
    /* the kernel runs a program that launches itself once more */
    if (!barnix->launch("/usr/bin/gouse")) launch_requested = 1;
}

/* ---------------------------------------------------------------- desktop shortcuts */
enum { SC_APPLICATION, SC_LINK, SC_DIRECTORY };
typedef struct { char file[160], name[64], exec[256], url[512], path[256]; int type, icon; } Shortcut;
#define MAX_SHORTCUTS 40
static Shortcut shortcuts[MAX_SHORTCUTS];
static int shortcut_count, shortcuts_loaded;
static int prefix_ci(const char *s, const char *prefix, int n)
{
    for (int i = 0; i < n; i++) { char a = s[i], b = prefix[i]; if (!a) return 0; if ((a | 32) != (b | 32) && a != b) return 0; }
    return 1;
}
static int gouse_exec(const char *exec) { return prefix_ci(exec, "gouse:", 6); }
static void desktop_dir(char *out, int capacity) { g_path_join(out, capacity, g_home(), "Desktop"); }
/* One Desktop Entry file; 0 when it has something to open. */
static int parse_shortcut(const char *file, Shortcut *sc)
{
    char text[4096];
    int n = g_read_file(file, text, sizeof(text) - 1);
    if (n <= 0) return -1;
    text[n] = 0;
    for (unsigned int i = 0; i < sizeof(*sc); i++) ((char *)sc)[i] = 0;
    g_copy(sc->file, file, sizeof(sc->file));
    sc->icon = -1;
    char type[32] = "Application", localized[64] = "";
    int in_entry = 1;
    for (char *line = text; *line;) {
        char *end = line; while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        int length = g_len(line);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (line[0] == '[') in_entry = g_equal(line, "[Desktop Entry]");
        else if (in_entry && line[0] != '#') {
            char *eq = line; while (*eq && *eq != '=') eq++;
            if (*eq) {
                *eq = 0;
                char *key = line, *value = eq + 1;
                int kl = g_len(key); while (kl && key[kl - 1] == ' ') key[--kl] = 0;
                while (*value == ' ') value++;
                if (g_equal(key, "Type")) g_copy(type, value, sizeof(type));
                else if (g_equal(key, "Name")) g_copy(sc->name, value, sizeof(sc->name));
                else if (g_equal(key, "Name[ru]")) g_copy(localized, value, sizeof(localized));
                else if (g_equal(key, "Exec")) g_copy(sc->exec, value, sizeof(sc->exec));
                else if (g_equal(key, "URL")) g_copy(sc->url, value, sizeof(sc->url));
                else if (g_equal(key, "Path")) g_copy(sc->path, value, sizeof(sc->path));
                else if (g_equal(key, "Icon")) sc->icon = g_icon_named(value);
            }
        }
        *end = saved; line = *end ? end + 1 : end;
    }
    if (g_russian && localized[0]) g_copy(sc->name, localized, sizeof(sc->name));
    sc->type = g_equal(type, "Link") ? SC_LINK : g_equal(type, "Directory") ? SC_DIRECTORY : SC_APPLICATION;
    if (!sc->name[0]) {   /* the file name without .desktop */
        const char *base = file;
        for (const char *p = file; *p; p++) if (*p == '/') base = p + 1;
        g_copy(sc->name, base, sizeof(sc->name));
        int l = g_len(sc->name);
        if (l > 8 && g_ends_ci(sc->name, ".desktop")) sc->name[l - 8] = 0;
    }
    if (sc->icon < 0) {
        if (sc->type == SC_LINK) sc->icon = ICON_BROWSER;
        else if (sc->type == SC_DIRECTORY) sc->icon = ICON_EXECUTIVE;
        else if (g_ends_ci(sc->exec, ".elf") || sc->exec[0] == '/') sc->icon = ICON_PROGRAM;
        else {
            sc->icon = ICON_PROGRAM;
            if (gouse_exec(sc->exec)) {
                const char *app_name = sc->exec + 6;
                for (int i = 0; i < g_app_count; i++) {
                    int l = g_len(g_apps[i]->name);
                    if (prefix_ci(app_name, g_apps[i]->name, l)) sc->icon = g_apps[i]->icon;
                }
            }
        }
    }
    return sc->type == SC_LINK ? (sc->url[0] ? 0 : -1) : sc->type == SC_DIRECTORY ? (sc->path[0] ? 0 : -1) : (sc->exec[0] ? 0 : -1);
}
static int write_entry(const char *file, const char *name, int type, const char *target)
{
    char text[1200];
    int n = g_format(text, sizeof(text), "[Desktop Entry]\nType=%s\nName=%s\n%s%s\n",
                     type == SC_LINK ? "Link" : type == SC_DIRECTORY ? "Directory" : "Application", name,
                     type == SC_LINK ? "URL=" : type == SC_DIRECTORY ? "Path=" : "Exec=", target);
    return g_write_file(file, text, n);
}
static void default_shortcuts(const char *dir)
{
    static const struct { const char *file, *name, *ru, *type, *key, *target, *icon; } defaults[] = {
        {"web.desktop", "Barnix on the web", "Barnix в сети", "Link", "URL", "about:home", "browser"},
        {"programs.desktop", "Programs", "Программы", "Directory", "Path", "/usr/bin", "executive"},
        {"wikipedia.desktop", "Wikipedia", "Википедия", "Link", "URL", "https://ru.m.wikipedia.org/", "browser"},
        {"snake.desktop", "Snake", "Змейка", "Application", "Exec", "/usr/bin/snake", "program"},
        {"glrunner.desktop", "GL Runner", "GL Гонка", "Application", "Exec", "/usr/bin/glrunner", "program"},
    };
    for (unsigned int i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        char file[200], text[600];
        g_path_join(file, sizeof(file), dir, defaults[i].file);
        int n = g_format(text, sizeof(text), "[Desktop Entry]\nType=%s\nName=%s\nName[ru]=%s\n%s=%s\nIcon=%s\n",
                         defaults[i].type, defaults[i].name, defaults[i].ru, defaults[i].key, defaults[i].target, defaults[i].icon);
        g_write_file(file, text, n);
    }
}
void g_desktop_reload(void)
{
    char dir[200];
    desktop_dir(dir, sizeof(dir));
    unsigned int cursor = 0;
    BarnixDirEntry entry;
    int result = barnix->directory(dir, &cursor, &entry);
    if (result < 0 && !shortcuts_loaded && !barnix->mkdir(dir)) default_shortcuts(dir);
    shortcuts_loaded = 1;
    shortcut_count = 0;
    cursor = 0;
    while (barnix->directory(dir, &cursor, &entry) > 0 && shortcut_count < MAX_SHORTCUTS) {
        if (entry.kind == 4 || !g_ends_ci(entry.name, ".desktop")) continue;
        char file[200];
        g_path_join(file, sizeof(file), dir, entry.name);
        if (!parse_shortcut(file, &shortcuts[shortcut_count])) shortcut_count++;
    }
    for (int i = 1; i < shortcut_count; i++) {   /* by name */
        Shortcut v = shortcuts[i]; int j = i;
        while (j > 0) {
            const char *a = v.name, *b = shortcuts[j - 1].name;
            while (*a && (*a | 32) == (*b | 32)) { a++; b++; }
            if ((*a | 32) >= (*b | 32)) break;
            shortcuts[j] = shortcuts[j - 1]; j--;
        }
        shortcuts[j] = v;
    }
    background_ready = 0; redraw = 1;
}
static int run_shortcut(const Shortcut *sc)
{
    if (sc->type == SC_LINK) { g_browse(sc->url); return 0; }
    if (sc->type == SC_DIRECTORY) return g_open(&app_executive, sc->path) ? 0 : -1;
    if (gouse_exec(sc->exec)) {
        const char *rest = sc->exec + 6;
        const GApp *app = 0; int skip = 0;
        for (int i = 0; i < g_app_count; i++) {   /* names may contain spaces: the longest match */
            int l = g_len(g_apps[i]->name);
            if (prefix_ci(rest, g_apps[i]->name, l) && (rest[l] == 0 || rest[l] == ' ') && l > skip) { app = g_apps[i]; skip = l; }
        }
        if (!app) return -1;
        const char *argument = rest + skip;
        while (*argument == ' ') argument++;
        return g_open(app, *argument ? argument : 0) ? 0 : -1;
    }
    g_run_program(0, sc->exec);
    return 0;
}
int g_open_shortcut(const char *path)
{
    Shortcut sc;
    if (parse_shortcut(path, &sc)) return -1;
    return run_shortcut(&sc);
}
/* A file name for a new shortcut called name. */
static void shortcut_file(const char *name, char *out, int capacity)
{
    char dir[200], base[64];
    desktop_dir(dir, sizeof(dir));
    int k = 0;
    for (const char *p = name; *p && k < 40; p++) {
        unsigned char c = (unsigned char)*p;
        base[k++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? (char)(c | (c >= 'A' && c <= 'Z' ? 32 : 0)) : c >= 0x80 ? 0 : '-';
        if (!base[k - 1]) k--;
    }
    while (k && base[k - 1] == '-') k--;
    base[k] = 0;
    if (!k) g_copy(base, "shortcut", sizeof(base));
    for (int i = 1; i < 100; i++) {
        char file[64];
        if (i == 1) g_format(file, sizeof(file), "%s.desktop", base);
        else g_format(file, sizeof(file), "%s-%d.desktop", base, i);
        g_path_join(out, capacity, dir, file);
        if (barnix->size(out) < 0) return;
    }
}
int g_shortcut_for(const char *target, const char *name)
{
    char dir[200];
    desktop_dir(dir, sizeof(dir));
    unsigned int cursor = 0; BarnixDirEntry entry;
    if (barnix->directory(dir, &cursor, &entry) < 0 && barnix->mkdir(dir)) return -1;
    int type = SC_APPLICATION;
    char exec[512], label[64];
    g_copy(exec, target, sizeof(exec));
    const char *base = target;
    for (const char *p = target; *p; p++) if (*p == '/' && p[1]) base = p + 1;
    g_copy(label, name && name[0] ? name : base, sizeof(label));
    int is_url = prefix_ci(target, "http://", 7) || prefix_ci(target, "https://", 8) || prefix_ci(target, "about:", 6) || prefix_ci(target, "www.", 4);
    if (is_url) {
        type = SC_LINK;
        if (prefix_ci(target, "www.", 4)) g_format(exec, sizeof(exec), "https://%s", target);
        if (!name || !name[0]) {   /* the host name */
            const char *h = exec; while (*h && *h != ':') h++;
            if (*h) h += 3;
            int k = 0; while (h[k] && h[k] != '/' && k < 60) { label[k] = h[k]; k++; }
            label[k] = 0;
            if (!k) g_copy(label, exec, sizeof(label));
        }
    } else if (g_app_named(target)) { g_format(exec, sizeof(exec), "gouse:%s", g_app_named(target)->name); if (!name || !name[0]) g_copy(label, g_app_named(target)->name, sizeof(label)); }
    else {
        cursor = 0;
        if (barnix->directory(target, &cursor, &entry) >= 0 && barnix->size(target) < 0) type = SC_DIRECTORY;
        else if (g_ends_ci(target, ".html") || g_ends_ci(target, ".htm")) { type = SC_LINK; g_format(exec, sizeof(exec), "file://%s", target); }
        else if (g_ends_ci(target, ".bmp")) g_format(exec, sizeof(exec), "gouse:Paint %s", target);
        else if (g_ends_ci(target, ".wav")) g_format(exec, sizeof(exec), "gouse:Media Player %s", target);
        else if (g_ends_ci(target, ".cards")) g_format(exec, sizeof(exec), "gouse:Cardfile %s", target);
        else {
            char head[4];
            int elf = barnix->read(target, 0, head, 4) == 4 && head[0] == 0x7f && head[1] == 'E';
            if (!elf && barnix->size(target) >= 0) g_format(exec, sizeof(exec), "gouse:Notepad %s", target);
        }
        if (!name || !name[0]) { int l = g_len(label); for (int i = l - 1; i > 0; i--) if (label[i] == '.') { if (l - i <= 6) label[i] = 0; break; } }
    }
    char file[200];
    shortcut_file(label, file, sizeof(file));
    int result = write_entry(file, label, type, exec);
    g_desktop_reload();
    return result ? -1 : 0;
}
void g_desktop_link(const char *name, const char *url) { g_shortcut_for(url, name); }
void g_browse(const char *url)
{
    GWindow *b = g_find(&app_browser);
    if (b) { g_focus(b); if (b->minimized) g_focus(b); browser_open_url(b, url); }
    else g_open(&app_browser, url);
}

/* ---------------------------------------------------------------- desktop */
static int app_icons(void) { int n = 0; for (int i = 0; i < g_app_count; i++) if (!(g_apps[i]->flags & APP_HIDDEN)) n++; return n; }
static int icon_count(void) { return app_icons() + shortcut_count; }
static const GApp *icon_app(int index)
{
    for (int i = 0; i < g_app_count; i++) if (!(g_apps[i]->flags & APP_HIDDEN) && index-- == 0) return g_apps[i];
    return 0;
}
static const Shortcut *icon_shortcut(int index)
{
    index -= app_icons();
    return index >= 0 && index < shortcut_count ? &shortcuts[index] : 0;
}
static void icon_rect(int index, int *x, int *y)
{
    int rows = (g_screen_h - TASKBAR_H - 8) / 76;
    if (rows < 1) rows = 1;
    *x = 8 + index / rows * 86; *y = 8 + index % rows * 76;
}
static void build_background(void)
{
    unsigned int base = g_desktop_colors[g_settings.desktop_color];
    unsigned int dark = (base >> 1 & 0x7f7f7f) + (base >> 2 & 0x3f3f3f);
    unsigned int light = base + ((0xffffff - base) >> 3 & 0x1f1f1f);
    BgxCanvas bg = {background, g_screen_w, g_screen_h};
    for (int y = 0; y < g_screen_h; y++)
        for (int x = 0; x < g_screen_w; x++) {
            unsigned int c = base;
            switch (g_settings.pattern) {
            case 1: if (x % 8 == 0 && y % 8 == 0) c = light; break;
            case 2: if (((x >> 2) ^ (y >> 2)) & 1) c = dark; break;
            case 3: if (y % 12 == 0 || (x + (y / 12 % 2) * 12) % 24 == 0) c = dark; break;
            case 4: if ((x + y) % 16 < 3) c = light; break;
            }
            background[y * g_screen_w + x] = c;
        }
    if (g_settings.pattern == 5) {   /* the Gouse: logo and name in the middle */
        int cx = g_screen_w / 2 + 40, cy = (g_screen_h - TASKBAR_H) / 2 - 40;
        bgx_blend(&bg, cx - 90, cy - 80, 180, 196, 0xffffff, 26);
        g_logo(&bg, cx - 64, cy - 72, 128);
        bgx_text_shadow(&bg, cx - bgx_text_width("Happy Gouse", 2) / 2, cy + 64, "Happy Gouse", 0xffffff, 2);
    }
    background_ready = 1;
}
static void draw_desktop(void)
{
    if (!background_ready) build_background();
    copy_pixels(screen_pixels, background, g_screen_w * g_screen_h);
    for (int i = 0; i < icon_count(); i++) {
        const GApp *app = icon_app(i);
        const Shortcut *sc = app ? 0 : icon_shortcut(i);
        int x, y; icon_rect(i, &x, &y);
        if (i == selected_icon) bgx_blend(&screen, x + 2, y, 82, 72, 0xffffff, 60);
        g_icon(&screen, x + 27, y + 4, app ? app->icon : sc->icon, 32);
        if (sc) g_icon(&screen, x + 26, y + 22, ICON_LINK, 16);   /* the shortcut arrow */
        const char *name = app ? app->name : sc->name;
        int w = app ? bgx_text_width(name, 1) : (bgx_text_width)(name, 1);
        if (w > 82) w = 82;
        int tx = x + 43 - w / 2;
        if (i == selected_icon) bgx_fill(&screen, tx - 2, y + 40, w + 4, 18, g_theme.highlight);
        BgxCanvas *c = &screen;
        if (app) { g_text_fit(c, tx + 1, y + 42, 82, name, 0x000000); g_text_fit(c, tx, y + 41, 82, name, 0xffffff); }
        else { g_text_fit_raw(c, tx + 1, y + 42, 82, name, 0x000000); g_text_fit_raw(c, tx, y + 41, 82, name, 0xffffff); }
    }
}

/* ---------------------------------------------------------------- Gouse menu and taskbar */
#define START_ROW 22
static int start_items(void) { return g_app_count + 3; }
static void start_geometry(int *x, int *y, int *w, int *h, int *columns, int *rows)
{
    int available = g_screen_h - TASKBAR_H - 60;
    *rows = available / START_ROW;
    if (*rows > start_items()) *rows = start_items();
    *columns = (start_items() + *rows - 1) / *rows;
    *w = 210 * *columns + 8; *h = 56 + *rows * START_ROW + 6;
    *x = 2; *y = g_screen_h - TASKBAR_H - *h;
}
static int start_hit(int mx, int my)
{
    int x, y, w, h, columns, rows;
    start_geometry(&x, &y, &w, &h, &columns, &rows);
    if (mx < x + 4 || my < y + 56 || mx >= x + w - 4 || my >= y + h - 6) return -1;
    int index = (mx - x - 4) / 210 * rows + (my - y - 56) / START_ROW;
    return index < start_items() ? index : -1;
}
static void draw_start(void)
{
    int x, y, w, h, columns, rows;
    start_geometry(&x, &y, &w, &h, &columns, &rows);
    bgx_blend(&screen, x + 5, y + 5, w, h, 0, 80);
    g_panel(&screen, x, y, w, h, g_theme.face, 0);
    for (int i = 0; i < 52; i++) {   /* header gradient */
        unsigned int a = g_theme.title_active, b = g_theme.title_active2;
        unsigned int r = ((a >> 16 & 255) * (52 - i) + (b >> 16 & 255) * i) / 52, g = ((a >> 8 & 255) * (52 - i) + (b >> 8 & 255) * i) / 52,
                     bl = ((a & 255) * (52 - i) + (b & 255) * i) / 52;
        bgx_fill(&screen, x + 3, y + 3 + i, w - 6, 1, BGX_RGB(r, g, bl));
    }
    g_logo(&screen, x + 6, y + 5, 48);
    bgx_text_shadow(&screen, x + 60, y + 8, "Happy Gouse", 0xffffff, 1);
    char user[64]; g_format(user, sizeof(user), "%s", barnix->current_user());
    g_text(&screen, x + 60, y + 28, user, 0xe0e8ff);
    for (int i = 0; i < start_items(); i++) {
        int ix = x + 4 + i / rows * 210, iy = y + 56 + i % rows * START_ROW;
        unsigned int fg = g_theme.text;
        if (i == start_hover) { bgx_fill(&screen, ix, iy, 206, START_ROW, g_theme.highlight); fg = g_theme.highlight_text; }
        const char *label; int icon;
        if (i < g_app_count) { label = g_apps[i]->name; icon = g_apps[i]->icon; }
        else if (i == g_app_count) { label = "GL Runner"; icon = ICON_PROGRAM; }
        else if (i == g_app_count + 1) { label = "Run program..."; icon = ICON_PROGRAM; }
        else { label = "Exit Happy Gouse"; icon = ICON_SHUTDOWN; }
        if (i == g_app_count + 1) bgx_fill(&screen, ix + 2, iy - 1, 202, 1, g_theme.shadow);
        g_icon(&screen, ix + 4, iy + 3, icon, 16);
        g_text(&screen, ix + 26, iy + 3, label, fg);
    }
}
static int task_button_width(void)
{
    int width = (g_screen_w - 100 - 96) / (order_count ? order_count : 1);
    return width > 160 ? 160 : width;
}
/* Taskbar buttons in opening order (slot order), not z-order. */
static GWindow *task_window(int n)
{
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].used && n-- == 0) return &windows[i];
    return 0;
}
static void draw_taskbar(void)
{
    int y = g_screen_h - TASKBAR_H;
    bgx_fill(&screen, 0, y, g_screen_w, TASKBAR_H, g_theme.face);
    bgx_fill(&screen, 0, y, g_screen_w, 1, g_theme.light);
    bgx_fill(&screen, 0, y + 1, g_screen_w, 1, g_theme.face);
    g_panel(&screen, 3, y + 4, 92, TASKBAR_H - 7, g_theme.face, start_open);
    g_logo(&screen, 8 + start_open, y + 8 + start_open, 16);
    g_text(&screen, 29 + start_open, y + 8 + start_open, "Gouse", g_theme.text);
    g_text(&screen, 30 + start_open, y + 8 + start_open, "Gouse", g_theme.text);
    int bw = task_button_width();
    for (int i = 0; i < order_count; i++) {
        GWindow *w = task_window(i);
        int x = 100 + i * bw, focused = g_focused(w);
        g_panel(&screen, x, y + 4, bw - 4, TASKBAR_H - 7, focused ? g_theme.light : g_theme.face, focused);
        g_icon(&screen, x + 5 + focused, y + 8 + focused, w->app->icon, 16);
        g_text_fit(&screen, x + 25 + focused, y + 8 + focused, bw - 34, w->title, g_theme.text);
    }
    GTime t; g_time(&t);
    char clock[32];
    int hour = g_settings.clock_24h ? t.hour : (t.hour % 12 ? t.hour % 12 : 12);
    if (g_settings.clock_seconds) g_format(clock, sizeof(clock), "%02d:%02d:%02d", hour, t.minute, t.second);
    else g_format(clock, sizeof(clock), "%02d:%02d", hour, t.minute);
    if (!g_settings.clock_24h) g_append(clock, t.hour < 12 ? " AM" : " PM", sizeof(clock));
    int cw = bgx_text_width(clock, 1) + 16;
    g_panel(&screen, g_screen_w - cw - 4, y + 4, cw, TASKBAR_H - 7, g_theme.face, 1);
    g_text(&screen, g_screen_w - cw + 4, y + 8, clock, g_theme.text);
}

/* ---------------------------------------------------------------- window chrome */
static void title_buttons(const GWindow *w, int *x0, int *y0)
{
    int ox, oy, ow, oh;
    outer(w, &ox, &oy, &ow, &oh);
    *x0 = ox + ow - BORDER - 3 * 20; *y0 = oy + BORDER + 2;
}
static int title_button_at(const GWindow *w, int mx, int my)
{
    int x0, y0;
    title_buttons(w, &x0, &y0);
    if (my < y0 || my >= y0 + 16 || mx < x0) return -1;
    int k = (mx - x0) / 20;
    return k < 3 && (mx - x0) % 20 < 18 ? k : -1;
}
static int pressed_chrome = -1;
static void draw_window(GWindow *w)
{
    int ox, oy, ow, oh, focused = g_focused(w);
    outer(w, &ox, &oy, &ow, &oh);
    if (chrome(w)) {
        bgx_blend(&screen, ox + 4, oy + 4, ow, oh, 0, 60);
        g_panel(&screen, ox, oy, ow, oh, g_theme.face, 0);
        int tx = ox + BORDER, ty = oy + BORDER, tw = ow - 2 * BORDER;
        unsigned int a = focused ? g_theme.title_active : g_theme.title_inactive;
        unsigned int b = focused ? g_theme.title_active2 : (g_theme.title_inactive + 0x101010) & 0xffffff;
        for (int i = 0; i < tw; i += 4) {
            int k = i * 256 / (tw ? tw : 1);
            unsigned int r = ((a >> 16 & 255) * (256 - k) + (b >> 16 & 255) * k) >> 8;
            unsigned int g = ((a >> 8 & 255) * (256 - k) + (b >> 8 & 255) * k) >> 8;
            unsigned int bl = ((a & 255) * (256 - k) + (b & 255) * k) >> 8;
            bgx_fill(&screen, tx + i, ty, tw - i < 4 ? tw - i : 4, TITLE_H, BGX_RGB(r, g, bl));
        }
        g_icon(&screen, tx + 2, ty + 2, w->app->icon, 16);
        g_text_fit(&screen, tx + 22, ty + 2, tw - 22 - 64, w->title, g_theme.title_text);
        int bx, by;
        title_buttons(w, &bx, &by);
        for (int k = 0; k < 3; k++) {
            int x = bx + k * 20, down = pressed_chrome == k && drag_window == w;
            g_panel(&screen, x, by, 18, 16, g_theme.face, down);
            unsigned int ink = g_theme.text;
            if (k == 0) bgx_fill(&screen, x + 5 + down, by + 10 + down, 8, 2, ink);
            else if (k == 1) {
                if (w->maximized) { bgx_frame(&screen, x + 6 + down, by + 3 + down, 7, 6, 1, ink); bgx_frame(&screen, x + 4 + down, by + 6 + down, 7, 6, 1, ink); }
                else { bgx_frame(&screen, x + 4 + down, by + 3 + down, 10, 9, 1, ink); bgx_fill(&screen, x + 4 + down, by + 4 + down, 10, 1, ink); }
            } else {
                g_line(&screen, x + 5 + down, by + 4 + down, x + 12 + down, by + 11 + down, ink);
                g_line(&screen, x + 6 + down, by + 4 + down, x + 13 + down, by + 11 + down, ink);
                g_line(&screen, x + 12 + down, by + 4 + down, x + 5 + down, by + 11 + down, ink);
                g_line(&screen, x + 13 + down, by + 4 + down, x + 6 + down, by + 11 + down, ink);
            }
        }
        if (w->app->menu_count) {
            int my = ty + TITLE_H, mx = tx + 2;
            bgx_fill(&screen, tx, my, tw, MENU_H, g_theme.menu);
            for (int m = 0; m < w->app->menu_count; m++) {
                const char *title = w->app->menus[m].title;
                int width = bgx_text_width(title, 1) + 16;
                int open = menu_window == index_of(w) && menu_open == m;
                if (open) bgx_fill(&screen, mx, my + 1, width, MENU_H - 2, g_theme.highlight);
                g_text(&screen, mx + 8, my + 2, title, open ? g_theme.highlight_text : g_theme.text);
                mx += width;
            }
        }
        g_bevel(&screen, w->x - 1, w->y - 1, w->w + 2, w->h + 2, 1);
    }
    /* client */
    for (int y = 0; y < w->h; y++) {
        int sy = w->y + y;
        if (sy < 0 || sy >= g_screen_h) continue;
        int x0 = w->x < 0 ? -w->x : 0, x1 = w->x + w->w > g_screen_w ? g_screen_w - w->x : w->w;
        if (x1 > x0) copy_pixels(screen_pixels + sy * g_screen_w + w->x + x0, w->pixels + y * w->w + x0, x1 - x0);
    }
    if (chrome(w) && (w->app->flags & APP_RESIZABLE) && !w->maximized) {
        int gx = ox + ow - 14, gy = oy + oh - 14;
        for (int k = 0; k < 3; k++) {
            g_line(&screen, gx + 4 + k * 3, gy + 11, gx + 11, gy + 4 + k * 3, g_theme.light);
            g_line(&screen, gx + 5 + k * 3, gy + 11, gx + 11, gy + 5 + k * 3, g_theme.shadow);
        }
    }
    if (w->dialog.kind) draw_dialog(w);
}
static void menu_geometry(const GWindow *w, int m, int *x, int *y, int *width, int *height, int *count)
{
    int ox, oy, ow, oh;
    outer(w, &ox, &oy, &ow, &oh);
    *x = ox + BORDER + 2;
    for (int i = 0; i < m; i++) *x += bgx_text_width(w->app->menus[i].title, 1) + 16;
    *y = oy + BORDER + TITLE_H + MENU_H;
    int n = 0, widest = 80;
    while (n < 12 && w->app->menus[m].items[n].label) {
        int tw = bgx_text_width(w->app->menus[m].items[n].label, 1);
        if (tw > widest) widest = tw;
        n++;
    }
    *count = n; *width = widest + 40; *height = n * 20 + 6;
}
static int menu_title_at(const GWindow *w, int mx, int my)
{
    int ox, oy, ow, oh;
    outer(w, &ox, &oy, &ow, &oh);
    int y = oy + BORDER + TITLE_H, x = ox + BORDER + 2;
    if (!w->app->menu_count || my < y || my >= y + MENU_H) return -1;
    for (int m = 0; m < w->app->menu_count; m++) {
        int width = bgx_text_width(w->app->menus[m].title, 1) + 16;
        if (mx >= x && mx < x + width) return m;
        x += width;
    }
    return -1;
}
static int menu_item_at(const GWindow *w, int mx, int my)
{
    int x, y, width, height, count;
    menu_geometry(w, menu_open, &x, &y, &width, &height, &count);
    if (mx < x || mx >= x + width || my < y + 3 || my >= y + 3 + count * 20) return -1;
    int item = (my - y - 3) / 20;
    return w->app->menus[menu_open].items[item].label[0] == '-' ? -1 : item;
}
static void draw_menu(void)
{
    if (menu_open < 0 || menu_window < 0) return;
    GWindow *w = &windows[menu_window];
    int x, y, width, height, count;
    menu_geometry(w, menu_open, &x, &y, &width, &height, &count);
    bgx_blend(&screen, x + 4, y + 4, width, height, 0, 70);
    g_panel(&screen, x, y, width, height, g_theme.menu, 0);
    for (int i = 0; i < count; i++) {
        const GMenuItem *item = &w->app->menus[menu_open].items[i];
        int iy = y + 3 + i * 20;
        if (item->label[0] == '-') { bgx_fill(&screen, x + 6, iy + 9, width - 12, 1, g_theme.shadow); bgx_fill(&screen, x + 6, iy + 10, width - 12, 1, g_theme.light); continue; }
        unsigned int fg = g_theme.text;
        if (i == menu_hover) { bgx_fill(&screen, x + 3, iy, width - 6, 20, g_theme.highlight); fg = g_theme.highlight_text; }
        g_text(&screen, x + 20, iy + 2, item->label, fg);
    }
}

/* ---------------------------------------------------------------- hit testing */
static GWindow *window_at(int mx, int my)
{
    for (int i = order_count - 1; i >= 0; i--) {
        GWindow *w = &windows[order[i]];
        if (w->minimized) continue;
        int ox, oy, ow, oh;
        outer(w, &ox, &oy, &ow, &oh);
        if (mx >= ox && my >= oy && mx < ox + ow && my < oy + oh) return w;
    }
    return 0;
}
static int fullscreen_active(void) { GWindow *top = top_window(); return top && top->fullscreen && !top->minimized; }
static int zone_at(GWindow *w, int x, int y)
{
    for (int i = w->zone_count - 1; i >= 0; i--) {
        GZone *z = &w->zones[i];
        if (x >= z->x && y >= z->y && x < z->x + z->w && y < z->y + z->h) return z->id;
    }
    return -1;
}
static void client_mouse(GWindow *w, int type, unsigned int button)
{
    GEvent e = {0};
    e.type = type; e.x = mouse_x - w->x; e.y = mouse_y - w->y; e.button = button;
    send(w, &e);
}

static void open_icon(int index)
{
    const GApp *app = icon_app(index);
    const Shortcut *sc = icon_shortcut(index);
    if (app) g_open(app, 0);
    else if (sc) { Shortcut copy = *sc; if (run_shortcut(&copy)) g_error(top_window(), "Happy Gouse", "This shortcut cannot be opened."); }
}

/* ---------------------------------------------------------------- the desktop's context menu */
#define DESK_BASE 0x7e00
enum { DESK_OPEN = DESK_BASE, DESK_RENAME, DESK_DELETE, DESK_PROPERTIES, DESK_NEW, DESK_LINK, DESK_REFRESH, DESK_ARRANGE };
static int popup_open, popup_x, popup_y, popup_hover = -1, popup_count, popup_target = -1;
static GMenuItem popup_items[8];
static char popup_file[160];
#define POPUP_W 210
#define POPUP_ROW 22
static void popup_show(int target)
{
    popup_target = target;
    popup_count = 0;
    const Shortcut *sc = target >= 0 ? icon_shortcut(target) : 0;
    if (target >= 0) popup_items[popup_count++] = (GMenuItem){"Open", DESK_OPEN};
    if (sc) {
        g_copy(popup_file, sc->file, sizeof(popup_file));
        popup_items[popup_count++] = (GMenuItem){"Rename...", DESK_RENAME};
        popup_items[popup_count++] = (GMenuItem){"Delete shortcut", DESK_DELETE};
        popup_items[popup_count++] = (GMenuItem){"Properties", DESK_PROPERTIES};
    }
    if (target < 0) {
        popup_items[popup_count++] = (GMenuItem){"New shortcut...", DESK_NEW};
        popup_items[popup_count++] = (GMenuItem){"New web link...", DESK_LINK};
        popup_items[popup_count++] = (GMenuItem){"Refresh desktop", DESK_REFRESH};
    }
    popup_x = mouse_x; popup_y = mouse_y;
    if (popup_x + POPUP_W > g_screen_w) popup_x = g_screen_w - POPUP_W;
    if (popup_y + popup_count * POPUP_ROW + 6 > g_screen_h - TASKBAR_H) popup_y = g_screen_h - TASKBAR_H - popup_count * POPUP_ROW - 6;
    popup_open = 1; popup_hover = -1; redraw = 1;
}
static int popup_hit(int mx, int my)
{
    if (mx < popup_x || mx >= popup_x + POPUP_W || my < popup_y + 3) return -1;
    int i = (my - popup_y - 3) / POPUP_ROW;
    return i < popup_count ? i : -1;
}
static void draw_popup(void)
{
    if (!popup_open) return;
    int h = popup_count * POPUP_ROW + 6;
    bgx_blend(&screen, popup_x + 4, popup_y + 4, POPUP_W, h, 0, 70);
    g_panel(&screen, popup_x, popup_y, POPUP_W, h, g_theme.menu, 0);
    for (int i = 0; i < popup_count; i++) {
        int y = popup_y + 3 + i * POPUP_ROW;
        if (i == popup_hover) bgx_fill(&screen, popup_x + 3, y, POPUP_W - 6, POPUP_ROW, g_theme.highlight);
        g_text(&screen, popup_x + 12, y + 3, popup_items[i].label, i == popup_hover ? g_theme.highlight_text : g_theme.text);
    }
}
static GWindow *prompt_host(void)
{
    GWindow *host = top_window();
    if (!host || host->minimized || host->dialog.kind) host = g_open(&app_executive, 0);
    return host;
}
static void popup_choose(int item)
{
    popup_open = 0; redraw = 1;
    if (item < 0) return;
    int id = popup_items[item].id;
    GWindow *host;
    switch (id) {
    case DESK_OPEN: if (popup_target >= 0) open_icon(popup_target); break;
    case DESK_RENAME: {
        Shortcut sc;
        if (parse_shortcut(popup_file, &sc)) break;
        if ((host = prompt_host())) g_prompt(host, DESK_RENAME, "Rename shortcut", "New name:", sc.name);
        break;
    }
    case DESK_DELETE: {
        Shortcut sc;
        char text[160];
        if (parse_shortcut(popup_file, &sc)) break;
        g_format(text, sizeof(text), "Delete the shortcut \"%s\"?\nWhat it opens stays where it is.", sc.name);
        if ((host = prompt_host())) g_confirm(host, DESK_DELETE, "Delete shortcut", text);
        break;
    }
    case DESK_PROPERTIES: {
        Shortcut sc;
        char text[256];
        if (parse_shortcut(popup_file, &sc)) break;
        g_format(text, sizeof(text), "%s\n%s\n\nFile: %s", sc.name, sc.type == SC_LINK ? sc.url : sc.type == SC_DIRECTORY ? sc.path : sc.exec, sc.file);
        if ((host = prompt_host())) g_message(host, "Shortcut", text);
        break;
    }
    case DESK_NEW: if ((host = prompt_host())) g_prompt(host, DESK_NEW, "New shortcut", "Program, folder, file or web address:", "/usr/bin/"); break;
    case DESK_LINK: if ((host = prompt_host())) g_prompt(host, DESK_LINK, "New web link", "Web address:", "https://"); break;
    case DESK_REFRESH: g_desktop_reload(); break;
    }
}
static int desktop_event(GWindow *w, GEvent *e)
{
    if (e->type == EV_PROMPT && e->id == DESK_NEW) { if (e->text[0] && g_shortcut_for(e->text, 0)) g_error(w, "New shortcut", "Cannot make the shortcut (is ~/Desktop writable?)."); return 1; }
    if (e->type == EV_PROMPT && e->id == DESK_LINK) {
        if (e->text[0] && !g_equal(e->text, "https://") && !g_equal(e->text, "http://") && g_shortcut_for(e->text, 0)) g_error(w, "New web link", "Cannot make the shortcut.");
        return 1;
    }
    if (e->type == EV_PROMPT && e->id == DESK_RENAME) {
        Shortcut sc;
        if (e->text[0] && !parse_shortcut(popup_file, &sc)) {
            char file[200];
            shortcut_file(e->text, file, sizeof(file));
            const char *target = sc.type == SC_LINK ? sc.url : sc.type == SC_DIRECTORY ? sc.path : sc.exec;
            if (!write_entry(file, e->text, sc.type, target)) {
                if (!g_equal(file, popup_file)) barnix->rm(popup_file);
            } else g_error(w, "Rename shortcut", "Cannot rename the shortcut.");
            g_desktop_reload();
        }
        return 1;
    }
    if (e->type == EV_CONFIRM && e->id == DESK_DELETE) { if (barnix->rm(popup_file)) g_error(w, "Delete shortcut", "Cannot delete the shortcut."); g_desktop_reload(); return 1; }
    return 0;
}
static void start_choose(int item)
{
    start_open = 0; redraw = 1;
    if (item < 0) return;
    if (item < g_app_count) { g_open(g_apps[item], 0); return; }
    GWindow *host = top_window();
    if (!host) host = g_open(&app_executive, 0);
    if (item == g_app_count) g_run_program(host, "/usr/bin/glrunner");
    else if (item == g_app_count + 1) g_prompt(host, CONFIRM_RUN + 1, "Run program", "Program to run (for example /usr/bin/snake):", "/usr/bin/");
    else g_confirm(host, CONFIRM_RUN + 2, "Exit Happy Gouse", "Close all windows and return to the shell?");
}

static void mouse_down(unsigned int button)
{
    unsigned int now = g_now();
    int is_double = now - last_click_time < 450 && mouse_x - last_click_x < 5 && last_click_x - mouse_x < 5 &&
                    mouse_y - last_click_y < 5 && last_click_y - mouse_y < 5 && button == 1;
    last_click_time = is_double ? 0 : now; last_click_x = mouse_x; last_click_y = mouse_y;
    redraw = 1;
    if (capture_window) { client_mouse(capture_window, EV_MOUSE_DOWN, button); return; }
    if (popup_open) {
        int item = popup_hit(mouse_x, mouse_y);
        if (item >= 0 && button == 1) { popup_choose(item); return; }
        popup_open = 0;
        if (item >= 0) return;
    }
    if (start_open) {
        int item = start_hit(mouse_x, mouse_y);
        int x, y, w, h, c, r; start_geometry(&x, &y, &w, &h, &c, &r);
        if (item >= 0 || (mouse_x >= x && mouse_y >= y && mouse_x < x + w && mouse_y < y + h)) { if (item >= 0) start_choose(item); return; }
        start_open = 0;
        if (mouse_y >= g_screen_h - TASKBAR_H && mouse_x < 96) return;
    }
    if (menu_open >= 0) {
        GWindow *w = &windows[menu_window];
        int item = menu_item_at(w, mouse_x, mouse_y), title = menu_title_at(w, mouse_x, mouse_y);
        if (item >= 0) {
            int id = w->app->menus[menu_open].items[item].id;
            menu_open = -1;
            GEvent e = {0}; e.type = EV_MENU; e.id = id; send(w, &e);
            return;
        }
        menu_open = -1;
        if (title >= 0) return;
    }
    if (!fullscreen_active() && mouse_y >= g_screen_h - TASKBAR_H) {
        if (mouse_x < 96) { start_open = 1; start_hover = -1; return; }
        int bw = task_button_width(), i = (mouse_x - 100) / bw;
        if (mouse_x >= 100 && i < order_count) {
            GWindow *w = task_window(i);
            if (g_focused(w)) minimize(w); else g_focus(w);
        }
        return;
    }
    GWindow *w = window_at(mouse_x, mouse_y);
    if (!w) {
        if (top_window() && !top_window()->minimized) blur(top_window());
        selected_icon = -1;
        for (int i = 0; i < icon_count(); i++) {
            int x, y; icon_rect(i, &x, &y);
            if (mouse_x >= x && mouse_y >= y && mouse_x < x + 86 && mouse_y < y + 72) {
                selected_icon = i;
                if (is_double) open_icon(i);
            }
        }
        if (button == 2) popup_show(selected_icon);
        return;
    }
    if (!g_focused(w)) g_focus(w);
    drag_window = w; drag_button = button;
    if (w->dialog.kind) {
        int ok_x, cancel_x, by;
        dialog_buttons(w, &ok_x, &cancel_x, &by);
        if (mouse_y >= by && mouse_y < by + 26) {
            if (mouse_x >= ok_x && mouse_x < ok_x + 88) w->dialog.pressed = 1;
            if (mouse_x >= cancel_x && mouse_x < cancel_x + 88) w->dialog.pressed = 2;
        }
        drag = DRAG_CHROME; pressed_chrome = -1;
        return;
    }
    int ox, oy, ow, oh;
    outer(w, &ox, &oy, &ow, &oh);
    int in_client = mouse_x >= w->x && mouse_y >= w->y && mouse_x < w->x + w->w && mouse_y < w->y + w->h;
    if (in_client) {
        drag = DRAG_CLIENT;
        w->pressed_zone = button == 1 ? zone_at(w, mouse_x - w->x, mouse_y - w->y) : -1;
        if (is_double) { GEvent e = {0}; e.type = EV_DOUBLE; e.x = mouse_x - w->x; e.y = mouse_y - w->y; e.button = 1; send(w, &e); }
        client_mouse(w, EV_MOUSE_DOWN, button);
        return;
    }
    int k = title_button_at(w, mouse_x, mouse_y);
    if (k >= 0) { drag = DRAG_CHROME; pressed_chrome = k; return; }
    int m = menu_title_at(w, mouse_x, mouse_y);
    if (m >= 0) { menu_window = index_of(w); menu_open = m; menu_hover = -1; drag = DRAG_NONE; return; }
    if ((w->app->flags & APP_RESIZABLE) && !w->maximized && mouse_x >= ox + ow - 16 && mouse_y >= oy + oh - 16) {
        drag = DRAG_SIZE; drag_dx = ox + ow - mouse_x; drag_dy = oy + oh - mouse_y; return;
    }
    if (mouse_y < oy + BORDER + TITLE_H) {
        if (is_double && (w->app->flags & APP_RESIZABLE)) { maximize(w, !w->maximized); drag = DRAG_NONE; return; }
        if (!w->maximized) { drag = DRAG_MOVE; drag_dx = mouse_x - w->x; drag_dy = mouse_y - w->y; }
        return;
    }
    drag = DRAG_NONE;
}
static void mouse_up(unsigned int button)
{
    redraw = 1;
    if (capture_window) { client_mouse(capture_window, EV_MOUSE_UP, button); return; }
    GWindow *w = drag_window;
    if (!w || !w->used || button != drag_button) { drag = DRAG_NONE; return; }
    if (w->dialog.kind && drag == DRAG_CHROME) {
        int ok_x, cancel_x, by, pressed = w->dialog.pressed, hit = 0;
        dialog_buttons(w, &ok_x, &cancel_x, &by);
        if (mouse_y >= by && mouse_y < by + 26) {
            if (mouse_x >= ok_x && mouse_x < ok_x + 88) hit = 1;
            if (mouse_x >= cancel_x && mouse_x < cancel_x + 88) hit = 2;
        }
        w->dialog.pressed = 0;
        if (hit && hit == pressed) dialog_finish(w, hit == 1);
    } else if (drag == DRAG_CHROME) {
        int k = title_button_at(w, mouse_x, mouse_y);
        if (k == pressed_chrome) {
            if (k == 0) minimize(w);
            else if (k == 1) { if (w->app->flags & APP_RESIZABLE) maximize(w, !w->maximized); }
            else if (k == 2) g_close(w);
        }
        pressed_chrome = -1;
    } else if (drag == DRAG_CLIENT) {
        int zone = w->pressed_zone;
        w->pressed_zone = -1;
        client_mouse(w, EV_MOUSE_UP, button);
        if (zone >= 0 && w->used && zone_at(w, mouse_x - w->x, mouse_y - w->y) == zone) {
            GEvent e = {0}; e.type = EV_COMMAND; e.id = zone; send(w, &e);
        }
    }
    drag = DRAG_NONE; drag_window = 0;
}
static void mouse_move(void)
{
    if (popup_open) { int h = popup_hit(mouse_x, mouse_y); if (h != popup_hover) { popup_hover = h; redraw = 1; } }
    if (start_open) { int h = start_hit(mouse_x, mouse_y); if (h != start_hover) { start_hover = h; redraw = 1; } }
    if (menu_open >= 0) {
        GWindow *w = &windows[menu_window];
        int h = menu_item_at(w, mouse_x, mouse_y), t = menu_title_at(w, mouse_x, mouse_y);
        if (t >= 0 && t != menu_open) { menu_open = t; h = -1; }
        if (h != menu_hover) { menu_hover = h; }
        redraw = 1;
    }
    GWindow *w = drag_window;
    if (!w || !w->used) return;
    if (drag == DRAG_MOVE) {
        int nx = mouse_x - drag_dx, ny = mouse_y - drag_dy, top = BORDER + TITLE_H + menu_height(w);
        if (ny < top) ny = top;
        if (ny > g_screen_h - TASKBAR_H - 8) ny = g_screen_h - TASKBAR_H - 8;
        if (nx + w->w < 40) nx = 40 - w->w;
        if (nx > g_screen_w - 40) nx = g_screen_w - 40;
        w->x = nx; w->y = ny; redraw = 1;
    } else if (drag == DRAG_SIZE) {
        int ow = mouse_x + drag_dx - (w->x - BORDER), oh = mouse_y + drag_dy - (w->y - BORDER - TITLE_H - menu_height(w));
        resize(w, ow - 2 * BORDER, oh - 2 * BORDER - TITLE_H - menu_height(w));
        g_invalidate(w);
    } else if (drag == DRAG_CLIENT) client_mouse(w, EV_MOUSE_MOVE, buttons);
}
/* Alt+Tab: bring the bottom window up, as Windows did. */
static void cycle_windows(void)
{
    if (order_count < 2) { if (order_count) g_focus(&windows[order[0]]); return; }
    g_focus(&windows[order[0]]);
}
static void key_press(const BgxEvent *k)
{
    GWindow *w = top_window();
    if (w && w->minimized) w = 0;
    redraw = 1;
    if (k->code == 0x23 && (bgx_key_held(0xdb) || bgx_key_held(0xdc))) {
        start_open = 0; menu_open = -1;
        g_open(&app_guide, 0);
        return;
    }
    if (alt() && k->code == BGX_KEY_TAB) { cycle_windows(); return; }
    if (alt() && k->code == 0x3e) { if (w) g_close(w); return; }   /* Alt+F4 */
    if ((g_ctrl() && k->code == BGX_KEY_ESC) || k->code == 0xdb || k->code == 0xdc) { start_open = !start_open; start_hover = -1; menu_open = -1; return; }
    if (start_open) {
        if (k->code == BGX_KEY_ESC) start_open = 0;
        else if (k->code == BGX_KEY_UP) start_hover = start_hover <= 0 ? start_items() - 1 : start_hover - 1;
        else if (k->code == BGX_KEY_DOWN) start_hover = (start_hover + 1) % start_items();
        else if (k->code == BGX_KEY_ENTER && start_hover >= 0) start_choose(start_hover);
        return;
    }
    if (menu_open >= 0) {
        GWindow *mw = &windows[menu_window];
        int x, y, width, height, count;
        menu_geometry(mw, menu_open, &x, &y, &width, &height, &count);
        if (k->code == BGX_KEY_ESC) menu_open = -1;
        else if (k->code == BGX_KEY_DOWN || k->code == BGX_KEY_UP) {
            int step = k->code == BGX_KEY_DOWN ? 1 : count - 1;
            for (int i = 0; i < count; i++) {
                menu_hover = ((menu_hover < 0 ? (step == 1 ? -1 : 0) : menu_hover) + step) % count;
                if (mw->app->menus[menu_open].items[menu_hover].label[0] != '-') break;
            }
        } else if (k->code == BGX_KEY_LEFT) { menu_open = (menu_open + mw->app->menu_count - 1) % mw->app->menu_count; menu_hover = -1; }
        else if (k->code == BGX_KEY_RIGHT) { menu_open = (menu_open + 1) % mw->app->menu_count; menu_hover = -1; }
        else if (k->code == BGX_KEY_ENTER && menu_hover >= 0) {
            GEvent e = {0}; e.type = EV_MENU; e.id = mw->app->menus[menu_open].items[menu_hover].id;
            menu_open = -1; send(mw, &e);
        }
        return;
    }
    if (!w) return;
    if (w->dialog.kind) {
        if (k->code == BGX_KEY_ESC) dialog_finish(w, 0);
        else if (k->code == BGX_KEY_ENTER) dialog_finish(w, 1);
        else if (w->dialog.kind == 2) {
            GEvent e = {0}; e.type = EV_KEY; e.code = k->code; e.key = k->key;
            g_edit_key((GEdit *)&w->dialog.edit, &e);
        } else if (w->dialog.kind == 3 && (k->key == 'y' || k->key == 'Y')) dialog_finish(w, 1);
        else if (w->dialog.kind == 3 && (k->key == 'n' || k->key == 'N')) dialog_finish(w, 0);
        g_invalidate(w);
        return;
    }
    if (k->code == 0x57) {   /* F11 */
        if (w->app->flags & APP_FULLSCREEN) g_toggle_fullscreen(w);
        else if (w->app->flags & APP_RESIZABLE) maximize(w, !w->maximized);
        return;
    }
    if (k->code == 0x44 && w->app->menu_count && !w->fullscreen) {   /* F10 opens the menu bar */
        menu_window = index_of(w); menu_open = 0; menu_hover = -1; return;
    }
    g_typed[k->code & 255] = 1;
    if (!k->repeat) g_pressed[k->code & 255] = 1;
    GEvent e = {0};
    e.type = EV_KEY; e.code = k->code; e.key = k->key; e.repeat = k->repeat;
    send(w, &e);
}

/* ---------------------------------------------------------------- splash and main loop */
static void splash(void)
{
    for (int step = 0; step <= 24; step++) {
        for (int y = 0; y < g_screen_h; y++) {
            unsigned int k = (unsigned int)y * 255 / g_screen_h;
            bgx_fill(&screen, 0, y, g_screen_w, 1, BGX_RGB(20 + k / 6, 60 + k / 4, 120 + k / 3));
        }
        int cx = g_screen_w / 2, cy = g_screen_h / 2 - 50;
        g_logo(&screen, cx - 64, cy - 90, 128);
        const char *name = "Happy Gouse";
        bgx_text_shadow(&screen, cx - bgx_text_width(name, 3) / 2, cy + 50, name, 0xffffff, 3);
        const char *line = "Version " GOUSE_VERSION " for Barnix  -  a friendly windowed environment";
        bgx_text_shadow(&screen, cx - bgx_text_width(line, 1) / 2, cy + 104, line, 0xdfe8ff, 1);
        g_panel(&screen, cx - 120, cy + 140, 240, 14, 0x203050, 1);
        bgx_fill(&screen, cx - 118, cy + 142, 236 * step / 24, 10, 0xffd84a);
        const char *note = step < 24 ? "Waking up the geese..." : "Honk!";
        bgx_text_shadow(&screen, cx - bgx_text_width(note, 1) / 2, cy + 162, note, 0xffffff, 1);
        bgx_present(&screen);
        barnix->sleep_ms(45);
        g_sound_tick(45);
    }
}

/* One frame: input, time slices, drawing. The main loop runs it; g_pump
 * runs it from inside long work (a page load), when busy windows wait. */
static unsigned int frame_last, frame_last_second;
static int frame_last_phase = -1, pumping;
static void frame(void)
{
    int width = g_screen_w, height = g_screen_h;
    unsigned int now = g_now();
    int dt = (int)(now - frame_last);
    frame_last = now;
    for (int i = 0; i < 256; i++) g_pressed[i] = g_typed[i] = 0;
    g_clipboard_sync();
    BgxEvent k;
    while (bgx_poll(&k)) {
        if (k.type == BGX_PRESS) key_press(&k);
        else if (k.type == BGX_RELEASE) {
            GWindow *w = top_window();
            if (w && !w->minimized && !w->dialog.kind && !start_open && menu_open < 0) {
                GEvent e = {0}; e.type=EV_KEY_UP; e.code=k.code; e.key=k.key; send(w,&e);
            }
        }
    }
    for (int i = 0; i < 256; i++) g_held[i] = (unsigned char)bgx_key_held(i);
    BgxMouse m;
    bgx_mouse(&m);
    g_look_dx = g_look_dy = 0;
    if (capture_window) { g_look_dx = m.dx; g_look_dy = m.dy; }
    else if (m.dx || m.dy) {
        static const int speed[6] = {4, 2, 3, 4, 6, 8};
        int fx = m.dx * speed[g_settings.mouse_speed] + mouse_rx, fy = m.dy * speed[g_settings.mouse_speed] + mouse_ry;
        mouse_x += fx / 4; mouse_y += fy / 4; mouse_rx = fx % 4; mouse_ry = fy % 4;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= width) mouse_x = width - 1;
        if (mouse_y >= height) mouse_y = height - 1;
        redraw = 1;
        mouse_move();
    }
    for (int b = 0; b < 3; b++) {
        unsigned int bit = 1U << b, button = b == 0 ? 1 : b == 1 ? 2 : 4;
        if (m.pressed & bit) mouse_down(button);
        if (m.released & bit) mouse_up(button);
    }
    if (m.wheel) {
        GWindow *w = capture_window ? capture_window : window_at(mouse_x, mouse_y);
        if (w && !w->dialog.kind) { GEvent e = {0}; e.type = EV_WHEEL; e.id = m.wheel; e.x = mouse_x - w->x; e.y = mouse_y - w->y; send(w, &e); }
    }
    g_sound_tick(dt);
    /* Time slices: every window runs, focused or not. */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        GWindow *w = &windows[i];
        if (w->used && w->app->tick && !w->busy) w->app->tick(w, dt);
    }
    int phase = (int)(now / 500) & 1;
    GWindow *top = top_window();
    if (phase != frame_last_phase) { frame_last_phase = phase; if (top && !top->minimized) g_invalidate(top); }
    if (now / 1000 != frame_last_second) { frame_last_second = now / 1000; redraw = 1; }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        GWindow *w = &windows[i];
        if (!w->used || w->minimized) continue;
        if (w->app->flags & APP_CONTINUOUS) {
            /* background games get every third frame, so the focused window stays quick */
            if (g_focused(w) || ++w->frame_skip >= 3) { w->dirty = 1; w->frame_skip = 0; }
        }
    }
    if (!draw_frame()) barnix->sleep_ms(4);
}
void g_pump(void)
{
    if (pumping || painting) return;
    pumping = 1;
    frame();
    pumping = 0;
}

int main(int argc, const char *const *argv)
{
    (void)argv;
    if (argc > 1) { barnix->println(4, "gouse - Happy Gouse windowed environment; no arguments"); return 1; }
    int width = 640, height = 400;
    if (bgx_display(&width, &height)) { barnix->println(4, "Happy Gouse needs a graphics display."); return 1; }
    settings_load();   /* the screen size may be chosen there */
    if (g_settings.screen_w >= 640 && g_settings.screen_h >= 480 && g_settings.screen_w <= 1920 && g_settings.screen_h <= 1200) {
        width = g_settings.screen_w; height = g_settings.screen_h;   /* the kernel switches the display to it (Legacy BIOS) */
    }
    if (width > 1920) width = 1920;
    if (height > 1200) height = 1200;
    if (width < 640 || height < 400) { width = 640; height = 400; }
    unsigned int bytes = 0;
    memory_base = barnix->memory(&bytes);
    memory_size = bytes;
    if (!memory_base || bytes < (unsigned int)width * height * 4 * 4 + (8U << 20)) {
        barnix->println(4, "Happy Gouse needs about 16 MiB of free memory above the program arena.");
        return 1;
    }
    g_screen_w = width; g_screen_h = height;
    screen_pixels = g_alloc((unsigned int)width * height * 4);
    background = g_alloc((unsigned int)width * height * 4);
    g_theme = g_themes[0];
    if (bgx_open(&screen, screen_pixels, width, height)) { barnix->println(4, "Happy Gouse: no graphics mode."); return 1; }
    g_settings_apply();
    g_sound_init();
    char session[160];
    session_path(session, sizeof(session));
    if (barnix->size(session) < 0) { g_sound(G_SOUND_STARTUP); splash(); }   /* coming back from a program: no splash */
    mouse_x = width / 2; mouse_y = height / 2;
    quiet++;
    g_desktop_reload();
    if (!session_restore()) g_open(&app_executive, 0);
    quiet--;
    frame_last = g_now();
    while (running) {
        frame();
        if (launch_requested) break;
    }
    if (launch_requested) {
        session_save();
        bgx_close();
        barnix->clear();
        return 0;
    }
    /* Let the goodbye tune finish (the kernel silences everything at exit). */
    g_sound(G_SOUND_EXIT);
    for (unsigned int start = g_now(), last_tick = start; g_sound_busy() && g_now() - start < 2000;) {
        barnix->sleep_ms(20);
        unsigned int now = g_now();
        g_sound_tick((int)(now - last_tick)); last_tick = now;
    }
    bgx_close();
    barnix->clear();
    return 0;
}

/* Paints the windows that changed and shows the screen: 0 when nothing did. */
static int draw_frame(void)
{
    if (painting) return 0;
    painting = 1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        GWindow *w = &windows[i];
        if (!w->used || w->minimized || !w->dirty) continue;
        w->zone_count = 0;
        w->dirty = 0;
        if (w->app->paint) w->app->paint(w, &w->canvas);
        redraw = 1;
    }
    int shown = redraw;
    if (redraw) {
        GWindow *top = top_window();
        if (fullscreen_active()) draw_window(top);
        else {
            draw_desktop();
            for (int i = 0; i < order_count; i++) if (!windows[order[i]].minimized) draw_window(&windows[order[i]]);
            draw_taskbar();
            draw_menu();
            draw_popup();
            if (start_open) draw_start();
        }
        if (!capture_window) bgx_cursor(&screen, mouse_x, mouse_y);
        bgx_present(&screen);
        redraw = 0;
    }
    painting = 0;
    return shown;
}
void g_flush(void) { draw_frame(); }

/* Run and exit confirmations arrive as events of whichever window asked. */
static int system_event(GWindow *w, GEvent *e)
{
    if ((e->type == EV_PROMPT || e->type == EV_CONFIRM) && e->id >= DESK_BASE && e->id < DESK_BASE + 0x40) return desktop_event(w, e);
    if (e->type == EV_CONFIRM && e->id == CONFIRM_RUN) {
        /* The kernel runs it after Gouse returns, then starts Gouse again. */
        if (barnix->launch(launch_path)) g_error(w, "Run program", "This program cannot be run (missing or not executable).");
        else launch_requested = 1;
        return 1;
    }
    if (e->type == EV_PROMPT && e->id == CONFIRM_RUN + 1) { if (e->text[0]) g_run_program(w, e->text); return 1; }
    if (e->type == EV_CONFIRM && e->id == CONFIRM_RUN + 2) { running = 0; return 1; }
    return 0;
}
