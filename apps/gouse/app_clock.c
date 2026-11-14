/* Clock: an analog face that fills the window, with digital time and date. */
#include "gouse.h"

typedef struct { int second, digital_only; } State;
#define S ((State *)w->state)

static void hand(BgxCanvas *c, int cx, int cy, int index60, int length, int thickness, unsigned int color)
{
    int x = cx + (g_sin60(index60) * length >> 16), y = cy - (g_sin60(index60 + 15) * length >> 16);
    g_thick_line(c, cx, cy, x, y, thickness, color);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    GTime t; g_time(&t);
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    char time[24], date[48];
    int hour = g_settings.clock_24h ? t.hour : (t.hour % 12 ? t.hour % 12 : 12);
    g_format(time, sizeof(time), "%02d:%02d:%02d%s", hour, t.minute, t.second, g_settings.clock_24h ? "" : t.hour < 12 ? " AM" : " PM");
    g_format(date, sizeof(date), "%s, %d %s %d", g_day(t.weekday), t.day, g_month_of(t.month), t.year);
    int text_h = 44, size = w->w < w->h - text_h ? w->w : w->h - text_h;
    if (S->digital_only || size < 60) {
        int scale = w->w / (bgx_text_width(time, 1) + 16);
        if (scale < 1) scale = 1;
        if (scale > 4) scale = 4;
        bgx_text_shadow(c, (w->w - bgx_text_width(time, scale)) / 2, w->h / 2 - 8 * scale - 10, time, g_theme.title_active, scale);
        g_text_center(c, 0, w->h / 2 + 8 * scale - 4, w->w, date, g_theme.text);
        return;
    }
    int r = size / 2 - 8, cx = w->w / 2, cy = 4 + size / 2;
    g_disc(c, cx + 3, cy + 3, r + 4, g_theme.shadow);
    g_disc(c, cx, cy, r + 4, g_theme.title_active);
    g_disc(c, cx, cy, r, 0xffffff);
    for (int k = 0; k < 60; k++) {
        int outer_r = r - 3, inner_r = k % 5 ? r - 7 : r - 14;
        int x0 = cx + (g_sin60(k) * outer_r >> 16), y0 = cy - (g_sin60(k + 15) * outer_r >> 16);
        int x1 = cx + (g_sin60(k) * inner_r >> 16), y1 = cy - (g_sin60(k + 15) * inner_r >> 16);
        g_thick_line(c, x0, y0, x1, y1, k % 5 ? 1 : 3, k % 15 ? 0x30343c : 0xd03030);
    }
    if (r > 50) {
        for (int k = 1; k <= 12; k++) {
            char n[4]; g_format(n, sizeof(n), "%d", k);
            int rr = r - 26, x = cx + (g_sin60(k * 5) * rr >> 16), y = cy - (g_sin60(k * 5 + 15) * rr >> 16);
            g_text(c, x - bgx_text_width(n, 1) / 2, y - 8, n, 0x30343c);
        }
    }
    hand(c, cx, cy, (t.hour % 12) * 5 + t.minute / 12, r * 5 / 10, r > 60 ? 6 : 4, 0x20242c);
    hand(c, cx, cy, t.minute, r * 75 / 100, r > 60 ? 4 : 3, 0x20242c);
    hand(c, cx, cy, t.second, r * 85 / 100, 1, 0xd03030);
    g_disc(c, cx, cy, 4, 0xd03030);
    g_text_center(c, 0, w->h - 38, w->w, time, g_theme.text);
    g_text_center(c, 0, w->h - 20, w->w, date, g_theme.text);
}
static void tick(GWindow *w, int dt)
{
    (void)dt;
    unsigned int second = g_now() / 1000;
    if ((int)second != S->second) { S->second = second; g_invalidate(w); }
}
static void event(GWindow *w, GEvent *e)
{
    if (e->type == EV_MENU) S->digital_only = e->id == 2;
    else if (e->type == EV_DOUBLE) S->digital_only = !S->digital_only;
}
static const GMenu menus[] = {{"View", {{"Analog", 1}, {"Digital", 2}}}};
const GApp app_clock = {"Clock", "The time and date", ICON_CLOCK, 240, 280, 140, 90,
                        APP_RESIZABLE | APP_SINGLE, menus, 1, sizeof(State), 0, event, tick, paint, 0};
