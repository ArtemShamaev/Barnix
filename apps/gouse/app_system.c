/* Control Panel, Clipboard viewer and About Happy Gouse. */
#include "gouse.h"

/* ---------------------------------------------------------------- Control Panel */
enum { ID_THEME = 1, ID_COLOR = 20, ID_PATTERN = 40, ID_SPEED = 60, ID_24H = 70, ID_SECONDS, ID_SAVE, ID_VOLUME = 80, ID_SOUNDS = 90, ID_TEST,
       ID_LANG = 100, ID_TAB = 110, ID_SCREEN = 120, ID_NEW_SHORTCUT = 150, ID_NEW_LINK, ID_DESKTOP_FOLDER,
       P_SHORTCUT = 160, P_LINK, C_SCREEN };
static const int screens[][2] = {{800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1360, 768}, {1440, 900}, {1600, 900}, {1920, 1080}};
#define SCREEN_COUNT ((int)(sizeof(screens) / sizeof(screens[0])))
static int control_tab, chosen_screen = -1;
static void control_paint(GWindow *w, BgxCanvas *c)
{
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    static const char *const tabs[3] = {"Desktop", "System", "Screen and shortcuts"};
    int tx = 8;
    for (int i = 0; i < 3; i++) {
        int tw = bgx_text_width(tabs[i], 1) + 20;
        g_toggle(w, c, ID_TAB + i, tx, 6, tw, 26, tabs[i], control_tab == i);
        tx += tw + 4;
    }
    bgx_fill(c, 6, 36, w->w - 12, 1, g_theme.shadow);
    int y = 44;
    if (control_tab == 0) {
        g_text(c, 10, y, "Window colors", g_theme.title_active); y += 20;
        for (int i = 0; i < g_theme_count; i++) {
            int x = 10 + i % 3 * 136, ry = y + i / 3 * 30;
            g_toggle(w, c, ID_THEME + i, x, ry, 130, 26, g_themes[i].name, i == g_settings.theme);
            bgx_fill(c, x + 6, ry + 6, 8, 14, g_themes[i].title_active);
        }
        y += (g_theme_count + 2) / 3 * 30 + 8;
        g_text(c, 10, y, "Desktop color", g_theme.title_active); y += 20;
        for (int i = 0; i < g_desktop_color_count; i++) {
            int x = 10 + i * 34;
            g_panel(c, x, y, 30, 30, g_desktop_colors[i], i == g_settings.desktop_color);
            if (i == g_settings.desktop_color) bgx_frame(c, x - 2, y - 2, 34, 34, 2, g_theme.text);
            g_zone(w, ID_COLOR + i, x, y, 30, 30);
        }
        y += 40;
        g_text(c, 10, y, "Desktop pattern", g_theme.title_active); y += 20;
        for (int i = 0; i < g_pattern_count; i++) g_toggle(w, c, ID_PATTERN + i, 10 + i % 3 * 136, y + i / 3 * 30, 130, 26, g_pattern_names[i], i == g_settings.pattern);
    } else if (control_tab == 1) {
        g_text(c, 10, y, "Mouse speed", g_theme.title_active); y += 20;
        static const char *const speeds[5] = {"Slowest", "Slow", "Normal", "Fast", "Fastest"};
        for (int i = 0; i < 5; i++) g_toggle(w, c, ID_SPEED + i, 10 + i * 82, y, 78, 26, speeds[i], i + 1 == g_settings.mouse_speed);
        y += 38;
        g_check(w, c, ID_24H, 10, y, "24-hour clock", g_settings.clock_24h);
        g_check(w, c, ID_SECONDS, 180, y, "Show seconds on the taskbar", g_settings.clock_seconds);
        y += 34;
        g_text(c, 10, y, "Sound", g_theme.title_active);
        g_text_fit(c, 70, y, w->w - 80, g_sound_name, g_theme.shadow); y += 20;
        static const char *const volumes[5] = {"Off", "25%", "50%", "75%", "100%"};
        for (int i = 0; i < 5; i++) g_toggle(w, c, ID_VOLUME + i, 10 + i * 66, y, 62, 26, volumes[i], i == g_settings.volume);
        g_button(w, c, ID_TEST, 344, y, 76, 26, "Test");
        y += 34;
        g_check(w, c, ID_SOUNDS, 10, y, g_sound_kind == BARNIX_SOUND_CARD ? "System sounds (start, windows, messages)"
                                                                           : "System sounds on the PC speaker", g_settings.sounds);
        y += 38;
        g_text(c, 10, y + 5, "Language", g_theme.title_active);
        g_toggle(w, c, ID_LANG, 110, y, 100, 26, "English", !g_russian);
        g_toggle(w, c, ID_LANG + 1, 216, y, 100, 26, "Русский", g_russian);
    } else {
        char line[64];
        g_text(c, 10, y, "Screen size", g_theme.title_active);
        g_format(line, sizeof(line), "now %d x %d", g_screen_w, g_screen_h);
        g_text(c, 16 + bgx_text_width("Screen size", 1), y, line, g_theme.shadow);
        y += 20;
        for (int i = 0; i < SCREEN_COUNT; i++) {
            char label[24];
            g_format(label, sizeof(label), "%d x %d", screens[i][0], screens[i][1]);
            g_toggle(w, c, ID_SCREEN + i, 10 + i % 3 * 136, y + i / 3 * 30, 130, 26, label, screens[i][0] == g_screen_w && screens[i][1] == g_screen_h);
        }
        y += (SCREEN_COUNT + 2) / 3 * 30 + 2;
        g_text_fit(c, 10, y, w->w - 20, "16:9 sizes fill wide monitors without stretching.", g_theme.shadow); y += 16;
        g_text_fit(c, 10, y, w->w - 20, "Happy Gouse starts again with the new size.", g_theme.shadow); y += 30;
        g_text(c, 10, y, "Desktop shortcuts", g_theme.title_active); y += 22;
        g_button(w, c, ID_NEW_SHORTCUT, 10, y, 200, 26, "New shortcut...");
        g_button(w, c, ID_NEW_LINK, 216, y, 200, 26, "New web link...");
        y += 32;
        g_button(w, c, ID_DESKTOP_FOLDER, 10, y, 200, 26, "Open the Desktop folder");
        y += 34;
        g_text_fit(c, 10, y, w->w - 20, "A shortcut opens a program, a folder, a file or a web page.", g_theme.shadow);
    }
    g_text_fit(c, 10, w->h - 22, w->w - 20, "Changes apply at once and are saved to ~/.gouse.cfg", g_theme.shadow);
}
static void control_event(GWindow *w, GEvent *e)
{
    if (e->type == EV_PROMPT) {
        if (e->text[0] && (e->id == P_SHORTCUT || (e->id == P_LINK && !g_equal(e->text, "https://"))))
            if (g_shortcut_for(e->text, 0)) g_error(w, "Control Panel", "Cannot make the shortcut (is ~/Desktop writable?).");
        return;
    }
    if (e->type == EV_CONFIRM && e->id == C_SCREEN && chosen_screen >= 0) {
        g_settings.screen_w = screens[chosen_screen][0]; g_settings.screen_h = screens[chosen_screen][1];
        g_settings_save();
        g_restart();
        return;
    }
    if (e->type != EV_COMMAND) return;
    int id = e->id;
    if (id >= ID_TAB && id < ID_TAB + 3) { control_tab = id - ID_TAB; g_invalidate(w); return; }
    if (id >= ID_SCREEN && id < ID_SCREEN + SCREEN_COUNT) {
        int k = id - ID_SCREEN;
        if (screens[k][0] == g_screen_w && screens[k][1] == g_screen_h) return;
        chosen_screen = k;
        char text[160];
        g_format(text, sizeof(text), "Switch the screen to %d x %d?\nHappy Gouse starts again; your windows come back.", screens[k][0], screens[k][1]);
        g_confirm(w, C_SCREEN, "Screen size", text);
        return;
    }
    if (id == ID_NEW_SHORTCUT) { g_prompt(w, P_SHORTCUT, "New shortcut", "Program, folder, file or web address:", "/usr/bin/"); return; }
    if (id == ID_NEW_LINK) { g_prompt(w, P_LINK, "New web link", "Web address:", "https://"); return; }
    if (id == ID_DESKTOP_FOLDER) {
        char path[200];
        g_path_join(path, sizeof(path), g_home(), "Desktop");
        extern const GApp app_executive;
        g_open(&app_executive, path);
        return;
    }
    if (id >= ID_THEME && id < ID_THEME + g_theme_count) g_settings.theme = id - ID_THEME;
    else if (id >= ID_COLOR && id < ID_COLOR + g_desktop_color_count) g_settings.desktop_color = id - ID_COLOR;
    else if (id >= ID_PATTERN && id < ID_PATTERN + g_pattern_count) g_settings.pattern = id - ID_PATTERN;
    else if (id >= ID_SPEED && id < ID_SPEED + 5) g_settings.mouse_speed = id - ID_SPEED + 1;
    else if (id == ID_24H) g_settings.clock_24h = !g_settings.clock_24h;
    else if (id == ID_SECONDS) g_settings.clock_seconds = !g_settings.clock_seconds;
    else if (id >= ID_VOLUME && id < ID_VOLUME + 5) g_settings.volume = id - ID_VOLUME;
    else if (id == ID_SOUNDS) g_settings.sounds = !g_settings.sounds;
    else if (id == ID_LANG || id == ID_LANG + 1) g_settings.lang = id - ID_LANG;
    else if (id == ID_TEST) {
        int sounds = g_settings.sounds;
        g_settings.sounds = 1; g_sound(G_SOUND_STARTUP); g_settings.sounds = sounds;
        return;
    }
    else return;
    g_settings_apply();
    g_settings_save();
}
const GApp app_control = {"Control Panel", "Colors, desktop, mouse, clock, sound, screen and shortcuts", ICON_CONTROL, 430, 400, 430, 400,
                          APP_SINGLE, 0, 0, 0, 0, control_event, 0, control_paint, 0};

/* ---------------------------------------------------------------- Clipboard */
enum { ID_CLEAR = 1 };
static void clip_paint(GWindow *w, BgxCanvas *c)
{
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    g_panel(c, 6, 6, w->w - 12, w->h - 44, g_theme.window, 1);
    if (!g_clipboard_len) g_text(c, 14, 12, "The clipboard is empty. Copy text in Notepad or Cardfile.", g_theme.shadow);
    int x = 0, y = 0, cols = (w->w - 28) / 8, rows = (w->h - 56) / 16;
    for (int i = 0; i < g_clipboard_len && y < rows;) {
        if (g_clipboard[i] == '\n') { x = 0; y++; i++; continue; }
        if (x >= cols) { x = 0; y++; continue; }
        int next = g_utf8_next(g_clipboard, g_clipboard_len, i);
        char glyph[5]; int n = next - i;
        for (int k = 0; k < n && k < 4; k++) glyph[k] = g_clipboard[i + k];
        glyph[n < 4 ? n : 4] = 0;
        g_text(c, 14 + x * 8, 10 + y * 16, glyph, g_theme.text);
        x++; i = next;
    }
    char info[48]; g_format(info, sizeof(info), "%d bytes", g_clipboard_len);
    g_text(c, 10, w->h - 30, info, g_theme.text);
    g_button(w, c, ID_CLEAR, w->w - 96, w->h - 34, 88, 26, "Clear");
}
static void clip_event(GWindow *w, GEvent *e) { (void)w; if (e->type == EV_COMMAND && e->id == ID_CLEAR) g_clipboard_set("", 0); }
const GApp app_clipboard = {"Clipboard", "See what was copied", ICON_CLIPBOARD, 400, 260, 260, 160,
                            APP_RESIZABLE | APP_SINGLE, 0, 0, 0, 0, clip_event, 0, clip_paint, 0};

/* ---------------------------------------------------------------- About */
enum { ID_OK = 1 };
static void about_paint(GWindow *w, BgxCanvas *c)
{
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    for (int y = 0; y < 150; y++) {
        unsigned int k = y * 255 / 150;
        bgx_fill(c, 0, y, w->w, 1, BGX_RGB(30 + k / 5, 90 + k / 4, 170 + k / 4 > 255 ? 255 : 170 + k / 4));
    }
    g_logo(c, 16, 11, 128);
    bgx_text_shadow(c, 160, 30, "Happy Gouse", 0xffffff, 2);
    bgx_text_shadow(c, 160, 66, "Version " GOUSE_VERSION, 0xe8f0ff, 1);
    bgx_text_shadow(c, 160, 86, "A windowed environment", 0xe8f0ff, 1);
    bgx_text_shadow(c, 160, 104, "for Barnix", 0xe8f0ff, 1);
    char line[96]; int y = 162, open = 0;
    for (const GApp *const *app = g_apps; app < g_apps + g_app_count; app++) if (g_find(*app)) open++;
    g_format(line, sizeof(line), "Screen: %d x %d, true color", g_screen_w, g_screen_h); g_text(c, 16, y, line, g_theme.text); y += 18;
    g_format(line, sizeof(line), "Memory: %u KiB free of %u KiB", g_memory_free() >> 10, g_memory_total() >> 10); g_text(c, 16, y, line, g_theme.text); y += 18;
    g_format(line, sizeof(line), "Built-in applications: %d, running: %d", g_app_count, open); g_text(c, 16, y, line, g_theme.text); y += 18;
    g_format(line, sizeof(line), "Processor cores: %u", bgx_cpus()); g_text(c, 16, y, line, g_theme.text); y += 18;
    g_format(line, sizeof(line), "Sound: %s", g_tr(g_sound_name)); g_text_fit(c, 16, y, w->w - 32, line, g_theme.text); y += 18;
    g_format(line, sizeof(line), "User: %s", barnix->current_user()); g_text(c, 16, y, line, g_theme.text); y += 26;
    g_text(c, 16, y, "Everything runs at once: each window gets", g_theme.shadow); y += 16;
    g_text(c, 16, y, "its time slice every frame. Honk!", g_theme.shadow);
    g_button(w, c, ID_OK, w->w - 100, w->h - 36, 90, 26, "OK");
}
static void about_event(GWindow *w, GEvent *e)
{
    if ((e->type == EV_COMMAND && e->id == ID_OK) || (e->type == EV_KEY && (e->key == '\n' || e->code == BGX_KEY_ESC))) g_close(w);
}
const GApp app_about = {"About Gouse", "Version and system information", ICON_ABOUT, 400, 366, 400, 366,
                        APP_SINGLE, 0, 0, 0, 0, about_event, 0, about_paint, 0};
