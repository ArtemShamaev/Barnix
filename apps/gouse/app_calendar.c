/* Calendar: month view with notes for any day, kept in ~/calendar.txt. */
#include "gouse.h"

#define MAX_NOTES 128
enum { ID_PREV = 1, ID_NEXT, ID_TODAY, ID_SAVE, ID_DAY = 100 };
typedef struct { int year, month, day; char text[72]; } Note;
typedef struct {
    int year, month, day;
    Note notes[MAX_NOTES];
    int count;
    GEdit edit;
    int editing;
} State;
#define S ((State *)w->state)

static void notes_path(char *out, int capacity) { g_path_join(out, capacity, g_home(), "calendar.txt"); }
static Note *find(State *s, int y, int m, int d)
{
    for (int i = 0; i < s->count; i++) if (s->notes[i].year == y && s->notes[i].month == m && s->notes[i].day == d) return &s->notes[i];
    return 0;
}
static void load(State *s)
{
    char path[160], data[MAX_NOTES * 90];
    notes_path(path, sizeof(path));
    int n = g_read_file(path, data, sizeof(data) - 1);
    s->count = 0;
    if (n <= 0) return;
    data[n] = 0;
    for (char *line = data; *line && s->count < MAX_NOTES;) {
        char *end = line; while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        /* YYYY-MM-DD text */
        if (end - line > 11 && line[4] == '-' && line[7] == '-') {
            Note *note = &s->notes[s->count++];
            note->year = g_atoi(line); note->month = g_atoi(line + 5); note->day = g_atoi(line + 8);
            g_copy(note->text, line + 11, sizeof(note->text));
        }
        *end = saved; line = *end ? end + 1 : end;
    }
}
static void save(GWindow *w)
{
    State *s = S;
    char path[160], data[MAX_NOTES * 90]; int n = 0;
    for (int i = 0; i < s->count; i++)
        if (s->notes[i].text[0]) n += g_format(data + n, sizeof(data) - n, "%04d-%02d-%02d %s\n", s->notes[i].year, s->notes[i].month, s->notes[i].day, s->notes[i].text);
    notes_path(path, sizeof(path));
    if (g_write_file(path, data, n)) g_error(w, "Calendar", "Cannot save calendar.txt in your home folder.");
}
static void select_day(State *s, int day)
{
    s->day = day;
    Note *note = find(s, s->year, s->month, day);
    g_edit_set(&s->edit, note ? note->text : "");
}
static void store(GWindow *w)
{
    State *s = S;
    Note *note = find(s, s->year, s->month, s->day);
    if (!note && s->edit.text[0] && s->count < MAX_NOTES) {
        note = &s->notes[s->count++];
        note->year = s->year; note->month = s->month; note->day = s->day;
    }
    if (note) g_copy(note->text, s->edit.text, sizeof(note->text));
    save(w);
}
static void open(GWindow *w, const char *argument)
{
    (void)argument;
    GTime t; g_time(&t);
    S->year = t.year; S->month = t.month;
    load(S);
    select_day(S, t.day);
}
static void move_month(State *s, int delta)
{
    s->month += delta;
    if (s->month < 1) { s->month = 12; s->year--; }
    if (s->month > 12) { s->month = 1; s->year++; }
    int days = g_days_in_month(s->year, s->month);
    select_day(s, s->day > days ? days : s->day);
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    GTime t; g_time(&t);
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    g_button(w, c, ID_PREV, 8, 8, 30, 24, "<");
    g_button(w, c, ID_NEXT, w->w - 38, 8, 30, 24, ">");
    char title[40]; g_format(title, sizeof(title), "%s %d", g_month(s->month), s->year);
    g_text_center(c, 40, 12, w->w - 80, title, g_theme.title_active);
    int gx = 8, gy = 40, cw = (w->w - 16) / 7, ch = (w->h - 40 - 74) / 7;
    for (int d = 0; d < 7; d++) {
        static const char *const names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        bgx_fill(c, gx + d * cw, gy, cw - 2, ch - 2, g_theme.title_active);
        g_text_center(c, gx + d * cw, gy + (ch - 18) / 2, cw - 2, names[d], g_theme.title_text);
    }
    int first = (g_weekday(s->year, s->month, 1) + 6) % 7, days = g_days_in_month(s->year, s->month);
    for (int day = 1; day <= days; day++) {
        int cell = first + day - 1, x = gx + cell % 7 * cw, y = gy + (cell / 7 + 1) * ch;
        int today = day == t.day && s->month == t.month && s->year == t.year, chosen = day == s->day;
        unsigned int bg = chosen ? g_theme.highlight : cell % 7 >= 5 ? 0xf2e8e8 : g_theme.window;
        g_panel(c, x, y, cw - 2, ch - 2, bg, chosen);
        char n[4]; g_format(n, sizeof(n), "%d", day);
        g_text(c, x + 4, y + 2, n, chosen ? g_theme.highlight_text : today ? 0xd03030 : g_theme.text);
        if (today) bgx_frame(c, x + 1, y + 1, cw - 4, ch - 4, 2, 0xd03030);
        if (find(s, s->year, s->month, day) && find(s, s->year, s->month, day)->text[0]) g_disc(c, x + cw - 10, y + ch - 10, 3, chosen ? 0xffffff : 0xe0a020);
        g_zone(w, ID_DAY + day, x, y, cw - 2, ch - 2);
    }
    char label[64];
    g_format(label, sizeof(label), "Note for %d %s:", s->day, g_month_of(s->month));
    g_text(c, 8, w->h - 68, label, g_theme.text);
    g_edit_draw(c, 8, w->h - 48, w->w - 100, &s->edit, g_focused(w));
    g_button(w, c, ID_SAVE, w->w - 86, w->h - 48, 78, 22, "Save");
    g_button(w, c, ID_TODAY, w->w - 86, w->h - 72, 78, 22, "Today");
    g_text_fit(c, 8, w->h - 22, w->w - 16, "Type a note and press Enter. PgUp/PgDn change the month.", g_theme.shadow);
}
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_COMMAND) {
        if (e->id == ID_PREV) move_month(s, -1);
        else if (e->id == ID_NEXT) move_month(s, 1);
        else if (e->id == ID_TODAY) { GTime t; g_time(&t); s->year = t.year; s->month = t.month; select_day(s, t.day); }
        else if (e->id == ID_SAVE) store(w);
        else if (e->id > ID_DAY) select_day(s, e->id - ID_DAY);
    } else if (e->type == EV_KEY) {
        int days = g_days_in_month(s->year, s->month);
        if (e->code == 0xc9) move_month(s, -1);
        else if (e->code == 0xd1) move_month(s, 1);
        else if (e->code == BGX_KEY_UP && s->day > 7) select_day(s, s->day - 7);
        else if (e->code == BGX_KEY_DOWN && s->day + 7 <= days) select_day(s, s->day + 7);
        else if (g_edit_key(&s->edit, e) == 2) store(w);
    }
}
const GApp app_calendar = {"Calendar", "Months, days and notes", ICON_CALENDAR, 420, 380, 300, 300,
                           APP_RESIZABLE | APP_SINGLE, 0, 0, sizeof(State), open, event, 0, paint, 0};
