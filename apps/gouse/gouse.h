/* Happy Gouse: a windowed environment for Barnix in the spirit of Windows 1.
 * One native program holds the window manager and its built-in applications;
 * they all run at once, cooperatively: every frame each window gets its time
 * slice (tick), its input events and, when it changed, a repaint. */
#ifndef GOUSE_H
#define GOUSE_H
#include "../barnix_app.h"
#include <barnix_gfx.h>
#include <barnix_sound.h>
#include <stdarg.h>
#include "gouse_assets.h"

#define GOUSE_VERSION "1.1"
#define BORDER 4
#define TITLE_H 20
#define MENU_H 20
#define TASKBAR_H 32
#define MAX_WINDOWS 16
#define MAX_ZONES 160
#define FILE_LIMIT (16 * 1024 * 1024)   /* the Barnix file size limit */

/* ---------------------------------------------------------------- events */
enum {
    EV_KEY = 1,        /* code: BGX_KEY_*, key: character or KEY_* */
    EV_MOUSE_DOWN,     /* x, y in client coordinates; button: 1 left, 2 right */
    EV_MOUSE_UP,
    EV_MOUSE_MOVE,     /* also while a button is held outside the client */
    EV_WHEEL,          /* id: wheel steps, positive = towards the user */
    EV_COMMAND,        /* id: a button/zone was clicked */
    EV_MENU,           /* id: a menu item was chosen */
    EV_PROMPT,         /* id, text: an input dialog was accepted */
    EV_CONFIRM,        /* id: a yes/no dialog was accepted */
    EV_DOUBLE,         /* double click at x, y */
    EV_RESIZE,
    EV_FOCUS,
    EV_BLUR,
    EV_KEY_UP
};
typedef struct {
    int type, x, y, id, repeat;
    unsigned int button, code, key;
    const char *text;
} GEvent;

typedef struct { const char *label; int id; } GMenuItem;
typedef struct { const char *title; GMenuItem items[12]; } GMenu;   /* label "-" = separator */

typedef struct GWindow GWindow;
#define APP_RESIZABLE 1U
#define APP_CONTINUOUS 2U   /* tick may change the picture every frame */
#define APP_SINGLE 4U       /* at most one window */
#define APP_FULLSCREEN 8U   /* F11 toggles full screen */
#define APP_HIDDEN 16U      /* not listed on the desktop */
typedef struct {
    const char *name, *description;
    int icon, w, h, min_w, min_h;
    unsigned int flags;
    const GMenu *menus;
    int menu_count, state_size;
    void (*open)(GWindow *w, const char *argument);
    void (*event)(GWindow *w, GEvent *e);
    void (*tick)(GWindow *w, int dt);
    void (*paint)(GWindow *w, BgxCanvas *c);
    void (*close)(GWindow *w);
} GApp;

typedef struct { int id, x, y, w, h; } GZone;
typedef struct {
    int kind, id;          /* 0 none, 1 message, 2 prompt, 3 confirm */
    char title[64], text[256];
    struct { char text[256]; int cursor; } edit;
    int pressed;
} GDialog;

struct GWindow {
    const GApp *app;
    void *state;
    int used, x, y, w, h;               /* client rectangle on the screen */
    int saved_x, saved_y, saved_w, saved_h;
    int maximized, minimized, fullscreen;
    char title[96];
    unsigned int *pixels;               /* client buffer, screen-sized */
    BgxCanvas canvas;
    int dirty, frame_skip;
    int busy;                           /* working in the background: cannot be closed */
    GZone zones[MAX_ZONES];
    int zone_count, pressed_zone;
    GDialog dialog;
};

/* ---------------------------------------------------------------- theme */
typedef struct {
    const char *name;
    unsigned int title_active, title_active2, title_inactive, title_text, face, light, shadow, dark,
                 window, text, highlight, highlight_text, menu;
} GTheme;
typedef struct {
    int theme, desktop_color, pattern, mouse_speed, clock_24h, clock_seconds;
    int volume, sounds;   /* volume 0..4 (quarters), system sounds on/off */
    int lang;             /* -1 not chosen (Russian), 0 English, 1 Russian */
    int screen_w, screen_h;   /* 0: what the display reports */
} GSettings;
extern GSettings g_settings;
extern GTheme g_theme;
extern const GTheme g_themes[];
extern const int g_theme_count;
extern const unsigned int g_desktop_colors[];
extern const int g_desktop_color_count;
extern const char *const g_pattern_names[];
extern const int g_pattern_count;
void g_settings_apply(void);
void g_settings_save(void);
/* Starts Happy Gouse again (a new screen size): the windows come back. */
void g_restart(void);

/* ---------------------------------------------------------------- environment */
extern int g_screen_w, g_screen_h;
extern unsigned char g_held[256], g_pressed[256], g_typed[256];
extern char g_clipboard[16384];
extern int g_clipboard_len;
extern const GApp *const g_apps[];
extern const int g_app_count;
void *g_alloc(unsigned int bytes);
unsigned int g_memory_free(void);
unsigned int g_memory_total(void);
unsigned int g_now(void);
GWindow *g_open(const GApp *app, const char *argument);
GWindow *g_find(const GApp *app);
const GApp *g_app_named(const char *name);
void g_close(GWindow *w);
void g_set_title(GWindow *w, const char *title);
void g_invalidate(GWindow *w);
int g_focused(const GWindow *w);
void g_focus(GWindow *w);
/* Mouse look for games: while on, the cursor is hidden and g_look_* hold the
 * motion of this frame. Losing focus or Alt+Tab turns it off. */
void g_capture(GWindow *w, int on);
int g_captured(const GWindow *w);
extern int g_look_dx, g_look_dy;
void g_toggle_fullscreen(GWindow *w);
/* Runs a program inside a new Gouse Prompt window; g_run_full gives it the
 * whole screen (Happy Gouse comes back when it ends). */
void g_run_program(GWindow *from, const char *path);
void g_run_full(GWindow *from, const char *path);
int g_launch(const char *command_line);   /* full screen with arguments; 0 = accepted */
void g_resize(GWindow *w, int width, int height);
void g_clipboard_set(const char *text, int length);
void g_clipboard_sync(void);   /* picks up what other programs copied */
void g_quit(void);
void media_open_file(GWindow *w, const char *path);   /* Media Player */
/* Gouse Web: opens url in the browser (its window comes up when needed). */
void g_browse(const char *url);
void browser_open_url(GWindow *w, const char *url);
/* Repaints and shows the screen now: progress during long work (page loads). */
void g_flush(void);
/* One frame of the desktop (input, other windows, drawing) from inside
 * long work: busy windows get no ticks. */
void g_pump(void);
/* Desktop shortcuts (~/Desktop, *.desktop: freedesktop.org Desktop Entry
 * files): Type=Application with Exec=/usr/bin/program or Exec=gouse:App ARG,
 * Type=Link with URL=, Type=Directory with Path=. */
int g_open_shortcut(const char *path);
int g_shortcut_for(const char *target, const char *name);   /* makes one on the desktop: 0 or -1 */
void g_desktop_link(const char *name, const char *url);
void g_desktop_reload(void);
int g_shift(void);
int g_ctrl(void);
int g_alt(void);

/* ---------------------------------------------------------------- sound */
/* System sounds obey the Control Panel switch; from G_SOUND_REVEAL on they
 * belong to games and only follow the volume. */
enum { G_SOUND_STARTUP, G_SOUND_DING, G_SOUND_QUESTION, G_SOUND_EXIT, G_SOUND_ERROR,
       G_SOUND_OPEN, G_SOUND_CLOSE, G_SOUND_MINIMIZE, G_SOUND_MAXIMIZE,
       G_SOUND_REVEAL, G_SOUND_FLAG, G_SOUND_BOOM, G_SOUND_DISC, G_SOUND_SLIDE, G_SOUND_WIN, G_SOUND_COUNT };
extern int g_sound_kind;       /* BARNIX_SOUND_CARD, _SPEAKER, or 0 */
extern char g_sound_name[64];
void g_sound_init(void);
void g_sound(int which);       /* respects the Control Panel settings */
void g_sound_tick(int dt);     /* PC speaker melodies advance here */
int g_sound_busy(void);

/* ---------------------------------------------------------------- dialogs */
void g_message(GWindow *w, const char *title, const char *text);
void g_error(GWindow *w, const char *title, const char *text);   /* a message with the honk */
void g_message_sound(GWindow *w, const char *title, const char *text, int sound);   /* -1: silent */
void g_prompt(GWindow *w, int id, const char *title, const char *label, const char *initial);
void g_confirm(GWindow *w, int id, const char *title, const char *text);

/* ---------------------------------------------------------------- utilities */
int g_len(const char *s);
void g_copy(char *out, const char *s, int capacity);
void g_append(char *out, const char *s, int capacity);
int g_equal(const char *a, const char *b);
int g_equal_ci(const char *a, const char *b);
int g_ends_ci(const char *s, const char *suffix);
int g_format(char *out, int capacity, const char *format, ...);
int g_vformat(char *out, int capacity, const char *format, va_list args);
int g_atoi(const char *s);
unsigned long long g_udiv64(unsigned long long n, unsigned long long d, unsigned long long *remainder);
unsigned int g_random(void);
int g_isqrt(int v);
int g_sin60(int index);   /* 16.16 sine of index*6 degrees */
const char *g_home(void);
void g_path_join(char *out, int capacity, const char *dir, const char *name);
int g_read_file(const char *path, char *buffer, int capacity);
int g_write_file(const char *path, const char *data, int size);
int g_utf8_prev(const char *s, int pos);
int g_utf8_next(const char *s, int length, int pos);
int g_utf8_put(char *out, unsigned int codepoint);

typedef struct { int year, month, day, hour, minute, second, weekday; } GTime;
void g_time(GTime *t);
int g_days_in_month(int year, int month);
int g_weekday(int year, int month, int day);   /* 0 = Sunday */
extern const char *const g_month_names[12];
extern const char *const g_day_names[7];
const char *g_month(int month);      /* 1..12, translated ("October" / "Октябрь") */
const char *g_month_of(int month);   /* as in a date ("October" / "октября") */
const char *g_day(int weekday);      /* 0 = Sunday, short and translated */

/* ---------------------------------------------------------------- language */
/* Gouse speaks Russian or English (Control Panel; ~/.gouse.cfg lang=).
 * Text is written in English and translated where it is drawn: g_text,
 * bgx_text and friends look the whole string up in gouse_ru.h, g_format
 * translates its format, dialogs their title and text. The _raw variants
 * draw user data (file names, typed text) exactly as it is. */
extern int g_russian;
const char *g_tr(const char *english);
int g_tr_text(BgxCanvas *c, int x, int y, const char *s, unsigned int color, int scale);
int g_tr_shadow(BgxCanvas *c, int x, int y, const char *s, unsigned int color, int scale);
int g_tr_width(const char *s, int scale);
#define bgx_text(c, x, y, s, color, scale) g_tr_text(c, x, y, s, color, scale)
#define bgx_text_shadow(c, x, y, s, color, scale) g_tr_shadow(c, x, y, s, color, scale)
#define bgx_text_width(s, scale) g_tr_width(s, scale)
int g_text_raw(BgxCanvas *c, int x, int y, const char *s, unsigned int color);
int g_text_fit_raw(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color);

/* ---------------------------------------------------------------- drawing */
void g_bevel(BgxCanvas *c, int x, int y, int w, int h, int sunken);
void g_panel(BgxCanvas *c, int x, int y, int w, int h, unsigned int color, int sunken);
void g_line(BgxCanvas *c, int x0, int y0, int x1, int y1, unsigned int color);
void g_thick_line(BgxCanvas *c, int x0, int y0, int x1, int y1, int thickness, unsigned int color);
void g_disc(BgxCanvas *c, int cx, int cy, int r, unsigned int color);
void g_ring(BgxCanvas *c, int cx, int cy, int r, unsigned int color);
void g_ellipse(BgxCanvas *c, int x0, int y0, int x1, int y1, unsigned int color, int filled);
int g_text(BgxCanvas *c, int x, int y, const char *s, unsigned int color);
int g_text_fit(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color);
void g_text_center(BgxCanvas *c, int x, int y, int width, const char *s, unsigned int color);
void g_icon(BgxCanvas *c, int x, int y, int icon, int size);
void g_logo(BgxCanvas *c, int x, int y, int size);   /* 16, 48 or 128 */
int g_icon_named(const char *name);                   /* "notepad" -> ICON_NOTEPAD, -1 when unknown */
void g_zone(GWindow *w, int id, int x, int y, int width, int height);
void g_button(GWindow *w, BgxCanvas *c, int id, int x, int y, int width, int height, const char *label);
void g_toggle(GWindow *w, BgxCanvas *c, int id, int x, int y, int width, int height, const char *label, int on);
void g_check(GWindow *w, BgxCanvas *c, int id, int x, int y, const char *label, int checked);
void g_scrollbar(BgxCanvas *c, int x, int y, int h, int total, int first, int visible);

typedef struct { char text[256]; int cursor; } GEdit;
void g_edit_set(GEdit *e, const char *text);
int g_edit_key(GEdit *e, const GEvent *ev);   /* 0 ignored, 1 changed, 2 Enter, 3 Esc */
void g_edit_draw(BgxCanvas *c, int x, int y, int width, const GEdit *e, int focused);

/* List rows are 18 px high. */
#define LIST_ROW 18
typedef const char *(*GListText)(void *context, int index, int *icon);
void g_list_draw(BgxCanvas *c, int x, int y, int w, int h, int count, int first, int selected,
                 GListText text, void *context, int focused);
int g_list_scroll(int count, int first, int selected, int rows);

/* Multi-line UTF-8 text editor shared by Notepad and Cardfile. */
/* Optional syntax colors: highlight() gives each byte of a line a class
 * (GTE_*) and carries state (an open comment) to the next line. */
enum { GTE_TEXT, GTE_KEYWORD, GTE_TYPE, GTE_STRING, GTE_COMMENT, GTE_NUMBER, GTE_DIRECTIVE, GTE_FUNCTION };
typedef void (*GteHighlight)(const char *line, int length, unsigned char *classes, int *state);
typedef struct {
    char *buffer;
    int capacity, length, cursor, anchor, top, left, goal, dragging, modified, shown;
    GteHighlight highlight;   /* 0: plain text */
    int gutter;               /* 1: line numbers on the left */
    const unsigned int *palette;   /* 0: the Gouse theme; else GTE_PAL_* colors (an editor's own theme) */
    int mark[2];              /* positions boxed (matching brackets), -1 for none */
    const int *dots; int dot_count;   /* 1-based lines with a red dot in the gutter (breakpoints) */
    int stop_line;            /* 1-based line drawn as the debugger's current one, 0 for none */
} GTextEdit;
enum { GTE_PAL_BACK, GTE_PAL_TEXT, GTE_PAL_GUTTER, GTE_PAL_NUMBER, GTE_PAL_NUMBER_CURRENT, GTE_PAL_SELECTION,
       GTE_PAL_CURSOR, GTE_PAL_LINE, GTE_PAL_MARK, GTE_PAL_SYNTAX, GTE_PAL_COUNT = GTE_PAL_SYNTAX + 7 };
void gte_init(GTextEdit *t, char *buffer, int capacity);
void gte_set(GTextEdit *t, const char *text, int length);
int gte_key(GTextEdit *t, const GEvent *e, int rows);
void gte_mouse(GTextEdit *t, const GEvent *e, int x, int y, int w, int h);
void gte_draw(GTextEdit *t, BgxCanvas *c, int x, int y, int w, int h, int focused);
int gte_selection(const GTextEdit *t, int *from, int *to);
void gte_insert(GTextEdit *t, const char *text, int length);
void gte_copy(GTextEdit *t, int cut);
int gte_line_of(const GTextEdit *t, int pos);          /* 0-based line of a position */
int gte_line_start(const GTextEdit *t, int line);      /* position where a line begins */
void gte_goto_line(GTextEdit *t, int line);            /* 0-based; moves the cursor there */
int gte_gutter_width(const GTextEdit *t);              /* pixels left of the text */
#endif
