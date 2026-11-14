/* Media Player: plays WAV files (PCM, 8 or 16 bit, mono or stereo) through
 * the sound card, with a position bar and a loop switch. */
#include "gouse.h"

enum { ID_OPEN = 1, ID_PLAY, ID_STOP, ID_LOOP, ID_PROMPT };
typedef struct {
    char path[256], info[96];
    BarnixSound clip;
    int loaded, voice, loop, playing;
    unsigned int started, length_ms, shown;
} State;
#define S ((State *)w->state)
/* One file at a time: the player is a single window. */
static unsigned char *data;

static void stop(GWindow *w)
{
    if (S->playing) bsnd_stop(S->voice);
    S->playing = 0;
    g_invalidate(w);
}

void media_open_file(GWindow *w, const char *path)
{
    State *s = S;
    stop(w);
    s->loaded = 0;
    g_copy(s->path, path, sizeof(s->path));
    if (!data) data = g_alloc(FILE_LIMIT);
    int size = barnix->size(path);
    if (!data) { g_copy(s->info, "Not enough memory.", sizeof(s->info)); return; }
    if (size <= 0 || size > FILE_LIMIT || barnix->read(path, 0, data, (unsigned int)size) != size) {
        g_copy(s->info, "Cannot read this file.", sizeof(s->info)); return;
    }
    if (bsnd_wav(data, (unsigned int)size, &s->clip)) {
        g_copy(s->info, "Not a PCM WAV file (8 or 16 bit, mono or stereo).", sizeof(s->info)); return;
    }
    s->loaded = 1;
    s->length_ms = (unsigned int)g_udiv64((unsigned long long)s->clip.frames * 1000, s->clip.rate, 0);
    g_format(s->info, sizeof(s->info), "%u Hz, %d-bit, %s, %u:%02u", s->clip.rate, s->clip.bits,
             s->clip.channels == 2 ? "stereo" : "mono", s->length_ms / 60000, s->length_ms / 1000 % 60);
    int slash = 0;
    for (int i = 0; path[i]; i++) if (path[i] == '/') slash = i + 1;
    g_set_title(w, path + slash);
}

static void play(GWindow *w)
{
    State *s = S;
    if (!s->loaded) return;
    if (g_sound_kind != BARNIX_SOUND_CARD) { g_error(w, "Media Player", "There is no sound card: WAV files need Intel HD Audio or AC'97."); return; }
    stop(w);
    s->clip.loop = (unsigned char)s->loop;
    s->voice = bsnd_play_clip(&s->clip);
    if (s->voice < 0) return;
    s->playing = 1; s->started = g_now();
}

static void open(GWindow *w, const char *argument)
{
    State *s = S;
    g_copy(s->info, "Open a WAV file, or double-click one in Executive.", sizeof(s->info));
    if (argument) { media_open_file(w, argument); play(w); }
}

static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    g_panel(c, 8, 8, w->w - 16, 64, 0x1c2230, 1);
    g_icon(c, 16, 24, ICON_MEDIA, 32);
    const char *name = s->path;
    for (const char *p = s->path; *p; p++) if (*p == '/') name = p + 1;
    g_text_fit(c, 58, 18, w->w - 76, s->loaded ? name : "No file", 0xffffff);
    g_text_fit(c, 58, 40, w->w - 76, s->info, 0x9fe39b);
    unsigned int position = 0;
    if (s->playing && s->length_ms) {
        position = g_now() - s->started;
        if (s->loop) position %= s->length_ms;
        else if (position > s->length_ms) position = s->length_ms;
    }
    int bar = w->w - 16;
    g_panel(c, 8, 82, bar, 14, g_theme.window, 1);
    if (s->length_ms) bgx_fill(c, 10, 84, (int)g_udiv64((unsigned long long)(bar - 4) * position, s->length_ms, 0), 10, g_theme.highlight);
    char time[32];
    g_format(time, sizeof(time), "%u:%02u / %u:%02u", position / 60000, position / 1000 % 60, s->length_ms / 60000, s->length_ms / 1000 % 60);
    g_text_center(c, 0, 100, w->w, time, g_theme.text);
    int y = w->h - 36;
    g_button(w, c, ID_OPEN, 8, y, 80, 28, "Open...");
    g_button(w, c, ID_PLAY, 96, y, 70, 28, "Play");
    g_button(w, c, ID_STOP, 174, y, 70, 28, "Stop");
    g_check(w, c, ID_LOOP, 256, y + 6, "Loop", s->loop);
}

static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_COMMAND) {
        if (e->id == ID_OPEN) g_prompt(w, ID_PROMPT, "Open", "WAV file to play:", s->path[0] ? s->path : g_home());
        else if (e->id == ID_PLAY) play(w);
        else if (e->id == ID_STOP) stop(w);
        else if (e->id == ID_LOOP) { s->loop = !s->loop; if (s->playing) play(w); }
        g_invalidate(w);
    } else if (e->type == EV_PROMPT && e->id == ID_PROMPT && e->text[0]) { media_open_file(w, e->text); play(w); }
    else if (e->type == EV_KEY && (e->key == ' ' || e->code == BGX_KEY_ENTER)) { if (s->playing) stop(w); else play(w); }
}

static void tick(GWindow *w, int dt)
{
    (void)dt;
    State *s = S;
    if (!s->playing) return;
    if (!s->loop && !bsnd_playing(s->voice)) { s->playing = 0; g_invalidate(w); return; }
    unsigned int tenth = (g_now() - s->started) / 100;
    if (tenth != s->shown) { s->shown = tenth; g_invalidate(w); }
}

static void close(GWindow *w) { stop(w); }

const GApp app_media = {"Media Player", "Play WAV sound files", ICON_MEDIA, 380, 170, 330, 170,
                        APP_SINGLE, 0, 0, sizeof(State), open, event, tick, paint, close};
