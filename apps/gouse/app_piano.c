/* Piano: two octaves under the mouse and the keyboard (Z S X D C ... for the
 * lower octave, Q 2 W 3 E ... for the upper), five instruments, a demo tune,
 * and a recorder that plays back or saves the take as a WAV file. */
#include "gouse.h"

#define RATE 32000
#define NOTE_MS 1100
#define CACHE 40
#define MAX_RECORD 600
#define SAVE_RATE 22050
#define SAVE_SECONDS 14
enum { PIANO, ORGAN, CHIP, FLUTE, SPEAKER, INSTRUMENTS };
static const char *const instrument_names[INSTRUMENTS] = {"Piano", "Organ", "Chiptune", "Flute", "PC speaker"};
enum { ID_INSTRUMENT = 1, ID_DOWN = 10, ID_UP, ID_DEMO, ID_RECORD, ID_PLAY, ID_SAVE, ID_SAVE_PROMPT };
/* Scan codes of the two keyboard rows, chromatic from C. */
static const unsigned char lower_keys[12] = {0x2c, 0x1f, 0x2d, 0x20, 0x2e, 0x2f, 0x22, 0x30, 0x23, 0x31, 0x24, 0x32};
static const unsigned char upper_keys[12] = {0x10, 0x03, 0x11, 0x04, 0x12, 0x13, 0x06, 0x14, 0x07, 0x15, 0x08, 0x16};
static const int black_after[7] = {1, 1, 0, 1, 1, 1, 0};   /* C D E F G A B: is there a black key after it */
static const int white_semitone[7] = {0, 2, 4, 5, 7, 9, 11};

/* "Ode to Joy" (Beethoven): MIDI note and length in sixteenths, 0 = rest. */
static const unsigned char ode[][2] = {
    {64, 4}, {64, 4}, {65, 4}, {67, 4}, {67, 4}, {65, 4}, {64, 4}, {62, 4}, {60, 4}, {60, 4}, {62, 4}, {64, 4}, {64, 6}, {62, 2}, {62, 8},
    {64, 4}, {64, 4}, {65, 4}, {67, 4}, {67, 4}, {65, 4}, {64, 4}, {62, 4}, {60, 4}, {60, 4}, {62, 4}, {64, 4}, {62, 6}, {60, 2}, {60, 8}};
#define SIXTEENTH 125

typedef struct {
    int instrument, octave, lit[24], speaker_ms;
    int demo, demo_at, demo_wait;               /* demo tune position */
    int recording, playing, play_at, play_wait, count;
    unsigned int record_start;
    unsigned int time[MAX_RECORD];
    unsigned char note[MAX_RECORD], sound[MAX_RECORD];
    char status[96];
} State;
#define S ((State *)w->state)

/* Synthesized notes shared by every window; a slot is reused only when it
 * no longer sounds, because the mixer reads the samples while it plays. */
static struct { short *samples; unsigned int frames; int instrument, note, voice; unsigned int used; } cache[CACHE];
static unsigned int use_clock;

static void synth(short *out, unsigned int capacity, unsigned int rate, int instrument, int note, unsigned int *frames)
{
    unsigned int hz = bsnd_note_hz(note);
    BsndTone tone = {BSND_PLUCK, hz, 0, NOTE_MS, 3, 300, 210};
    if (instrument == ORGAN) { tone.wave = BSND_ORGAN; tone.ms = 900; tone.attack_ms = 12; tone.release_ms = 220; tone.volume = 190; }
    else if (instrument == CHIP || instrument == SPEAKER) { tone.wave = BSND_SQUARE; tone.ms = 380; tone.attack_ms = 2; tone.release_ms = 120; tone.volume = 110; }
    else if (instrument == FLUTE) { tone.wave = BSND_SINE; tone.ms = 800; tone.attack_ms = 45; tone.release_ms = 300; tone.volume = 230; }
    *frames = bsnd_synth(out, capacity, rate, &tone);
    if (instrument == FLUTE) {   /* a breathy triangle an octave up */
        static short layer[NOTE_MS * 48];
        BsndTone breath = {BSND_TRIANGLE, hz * 2, 0, 800, 60, 300, 40};
        bsnd_mix(out, layer, bsnd_synth(layer, *frames, rate, &breath));
    }
}

static void play_note(GWindow *w, int note, int instrument)
{
    State *s = S;
    int key = note - (60 + s->octave * 12);
    if (key >= 0 && key < 24) { s->lit[key] = 180; g_invalidate(w); }
    if (s->recording && s->count < MAX_RECORD) {
        s->time[s->count] = g_now() - s->record_start; s->note[s->count] = (unsigned char)note; s->sound[s->count++] = (unsigned char)instrument;
    }
    if (!g_settings.volume) return;
    if (instrument == SPEAKER || g_sound_kind != BARNIX_SOUND_CARD) {
        bsnd_tone(bsnd_note_hz(note)); s->speaker_ms = 220;
        return;
    }
    int slot = -1;
    for (int i = 0; i < CACHE && slot < 0; i++) if (cache[i].samples && cache[i].instrument == instrument && cache[i].note == note) slot = i;
    if (slot < 0) {
        for (int i = 0; i < CACHE; i++) {
            if (cache[i].samples && bsnd_playing(cache[i].voice)) continue;
            if (slot < 0 || !cache[i].samples || (cache[slot].samples && cache[i].used < cache[slot].used)) slot = i;
            if (!cache[slot].samples) break;
        }
        if (slot < 0) return;   /* every slot sounds right now */
        if (!cache[slot].samples) cache[slot].samples = g_alloc(NOTE_MS * (RATE / 1000) * 2);
        if (!cache[slot].samples) return;
        synth(cache[slot].samples, NOTE_MS * (RATE / 1000), RATE, instrument, note, &cache[slot].frames);
        cache[slot].instrument = instrument; cache[slot].note = note;
    }
    cache[slot].used = ++use_clock;
    cache[slot].voice = bsnd_play(cache[slot].samples, cache[slot].frames, RATE, 256, (key - 12) * 6);
}

/* ---------------------------------------------------------------- layout */
#define KEYS_X 10
#define KEYS_Y 44
#define WHITE_W 41
#define WHITE_H 170
#define BLACK_W 26
#define BLACK_H 104
static int key_at(int x, int y)
{
    if (y < KEYS_Y || y >= KEYS_Y + WHITE_H || x < KEYS_X || x >= KEYS_X + 14 * WHITE_W) return -1;
    int white = (x - KEYS_X) / WHITE_W, octave = white / 7, in = white % 7;
    if (y < KEYS_Y + BLACK_H) {
        int left = KEYS_X + white * WHITE_W;
        if (black_after[in] && x >= left + WHITE_W - BLACK_W / 2) return octave * 12 + white_semitone[in] + 1;
        if (in > 0 && black_after[in - 1] && x < left + BLACK_W / 2) return octave * 12 + white_semitone[in] - 1;
    }
    return octave * 12 + white_semitone[in];
}

static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, w->h, g_theme.face);
    for (int i = 0; i < INSTRUMENTS; i++)
        g_toggle(w, c, ID_INSTRUMENT + i, 10 + i * 116, 8, 110, 26, instrument_names[i], i == s->instrument);
    g_panel(c, KEYS_X - 3, KEYS_Y - 3, 14 * WHITE_W + 6, WHITE_H + 6, 0x3a2414, 0);
    for (int k = 0; k < 14; k++) {
        int note = k / 7 * 12 + white_semitone[k % 7], x = KEYS_X + k * WHITE_W;
        bgx_fill(c, x, KEYS_Y, WHITE_W - 1, WHITE_H, s->lit[note] ? 0xffe28a : 0xfbfbf6);
        bgx_fill(c, x, KEYS_Y + WHITE_H - 6, WHITE_W - 1, 6, s->lit[note] ? 0xe8c060 : 0xd8d8d0);
        if (k % 7 == 0) {
            char name[8]; g_format(name, sizeof(name), "C%d", 4 + s->octave + k / 7);
            g_text_center(c, x, KEYS_Y + WHITE_H - 26, WHITE_W, name, 0x707070);
        }
    }
    for (int k = 0; k < 14; k++) {
        if (!black_after[k % 7]) continue;
        int note = k / 7 * 12 + white_semitone[k % 7] + 1, x = KEYS_X + k * WHITE_W + WHITE_W - BLACK_W / 2;
        bgx_fill(c, x, KEYS_Y, BLACK_W, BLACK_H, s->lit[note] ? 0xb07a20 : 0x18181c);
        bgx_fill(c, x + 3, KEYS_Y, BLACK_W - 6, BLACK_H - 8, s->lit[note] ? 0xd09a30 : 0x34343c);
    }
    int y = KEYS_Y + WHITE_H + 12;
    g_button(w, c, ID_DOWN, 10, y, 32, 26, "<");
    char octave[16]; g_format(octave, sizeof(octave), "C%d-B%d", 4 + s->octave, 5 + s->octave);
    g_text_center(c, 42, y + 5, 70, octave, g_theme.text);
    g_button(w, c, ID_UP, 112, y, 32, 26, ">");
    g_button(w, c, ID_DEMO, 160, y, 110, 26, s->demo ? "Stop tune" : "Ode to Joy");
    g_button(w, c, ID_RECORD, 280, y, 90, 26, s->recording ? "Stop" : "Record");
    if (s->recording) g_disc(c, 292, y + 13, 5, 0xe23d48);
    g_button(w, c, ID_PLAY, 380, y, 90, 26, s->playing ? "Stop" : "Play take");
    g_button(w, c, ID_SAVE, 480, y, 104, 26, "Save WAV...");
    y += 36;
    g_text_fit(c, 10, y, w->w - 20, s->status[0] ? s->status : "Keys: Z S X D C V G B H N J M and Q 2 W 3 E R 5 T 6 Y 7 U", g_theme.shadow);
}

static void open(GWindow *w, const char *argument)
{
    (void)argument;
    State *s = S;
    if (g_sound_kind != BARNIX_SOUND_CARD) {
        s->instrument = SPEAKER;
        g_copy(s->status, "No sound card: notes sound on the PC speaker.", sizeof(s->status));
    }
}

static void stop_all(GWindow *w)
{
    State *s = S;
    s->demo = s->playing = 0;
    bsnd_tone(0); s->speaker_ms = 0;
}

/* Render the take to a WAV file: mono, 16 bit, 22050 Hz, at most 14 s. */
static void save(GWindow *w, const char *path)
{
    State *s = S;
    if (!s->count) { g_message(w, "Piano", "Record something first: press Record and play."); return; }
    unsigned int frames = SAVE_RATE * SAVE_SECONDS, bytes = 44 + frames * 2;
    static unsigned char *file;
    static short *note;
    if (!file) file = g_alloc(bytes);
    if (!note) note = g_alloc(NOTE_MS * (SAVE_RATE / 1000 + 1) * 2);
    if (!file || !note) { g_error(w, "Piano", "Not enough memory to render the take."); return; }
    short *pcm = (short *)(file + 44);
    for (unsigned int i = 0; i < frames; i++) pcm[i] = 0;
    unsigned int end = 0;
    for (int i = 0; i < s->count; i++) {
        unsigned int at = s->time[i] / 10 * (SAVE_RATE / 100), n;
        if (at >= frames) break;
        synth(note, frames - at < NOTE_MS * (SAVE_RATE / 1000) ? frames - at : NOTE_MS * (SAVE_RATE / 1000), SAVE_RATE, s->sound[i], s->note[i], &n);
        bsnd_mix(pcm + at, note, n);
        if (at + n > end) end = at + n;
    }
    bytes = 44 + end * 2;
    static const unsigned char header[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        SAVE_RATE & 255, SAVE_RATE >> 8, 0, 0, (SAVE_RATE * 2) & 255, (SAVE_RATE * 2) >> 8 & 255, 0, 0, 2, 0, 16, 0,
        'd','a','t','a',0,0,0,0};
    for (int i = 0; i < 44; i++) file[i] = header[i];
    unsigned int riff = bytes - 8, data = end * 2;
    for (int i = 0; i < 4; i++) { file[4 + i] = (unsigned char)(riff >> (8 * i)); file[40 + i] = (unsigned char)(data >> (8 * i)); }
    if (g_write_file(path, (const char *)file, (int)bytes)) { g_error(w, "Piano", "Cannot write the file."); return; }
    g_format(s->status, sizeof(s->status), "Saved %s (%u KiB). Open it in Media Player.", path, bytes >> 10);
}

static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_COMMAND) {
        int id = e->id;
        if (id >= ID_INSTRUMENT && id < ID_INSTRUMENT + INSTRUMENTS) {
            s->instrument = id - ID_INSTRUMENT;
            if (g_sound_kind != BARNIX_SOUND_CARD && s->instrument != SPEAKER) {
                s->instrument = SPEAKER;
                g_copy(s->status, "No sound card: only the PC speaker can play.", sizeof(s->status));
            }
        } else if (id == ID_DOWN && s->octave > -2) s->octave--;
        else if (id == ID_UP && s->octave < 2) s->octave++;
        else if (id == ID_DEMO) { int on = !s->demo; stop_all(w); s->demo = on; s->demo_at = 0; s->demo_wait = 0; }
        else if (id == ID_RECORD) {
            s->recording = !s->recording;
            if (s->recording) { stop_all(w); s->count = 0; s->record_start = g_now(); g_copy(s->status, "Recording...", sizeof(s->status)); }
            else g_format(s->status, sizeof(s->status), "Recorded %d notes.", s->count);
        } else if (id == ID_PLAY) {
            int on = !s->playing && s->count;
            stop_all(w); s->recording = 0;
            s->playing = on; s->play_at = 0; s->play_wait = 0;
        } else if (id == ID_SAVE) {
            char path[160]; g_path_join(path, sizeof(path), g_home(), "tune.wav");
            g_prompt(w, ID_SAVE_PROMPT, "Save the take", "Save the recording as a WAV file:", path);
        }
        g_invalidate(w);
    } else if (e->type == EV_PROMPT && e->id == ID_SAVE_PROMPT && e->text[0]) { save(w, e->text); g_invalidate(w); }
    else if (e->type == EV_MOUSE_DOWN && e->button == 1) {
        int key = key_at(e->x, e->y);
        if (key >= 0) play_note(w, 60 + s->octave * 12 + key, s->instrument);
    } else if (e->type == EV_KEY && !e->repeat) {
        for (int i = 0; i < 12; i++) {
            if (e->code == lower_keys[i]) play_note(w, 60 + s->octave * 12 + i, s->instrument);
            if (e->code == upper_keys[i]) play_note(w, 72 + s->octave * 12 + i, s->instrument);
        }
    }
}

static void tick(GWindow *w, int dt)
{
    State *s = S;
    for (int i = 0; i < 24; i++)
        if (s->lit[i] && (s->lit[i] -= dt) <= 0) { s->lit[i] = 0; g_invalidate(w); }
    if (s->speaker_ms > 0 && (s->speaker_ms -= dt) <= 0) bsnd_tone(0);
    if (s->demo) {
        s->demo_wait -= dt;
        while (s->demo && s->demo_wait <= 0) {
            if (s->demo_at >= (int)(sizeof(ode) / sizeof(ode[0]))) { s->demo = 0; g_invalidate(w); break; }
            if (ode[s->demo_at][0]) play_note(w, ode[s->demo_at][0] + 12 * (s->octave + 1), s->instrument);
            s->demo_wait += ode[s->demo_at][1] * SIXTEENTH;
            s->demo_at++;
        }
    }
    if (s->playing) {
        s->play_wait += dt;
        while (s->playing && s->play_at < s->count && s->time[s->play_at] <= (unsigned int)s->play_wait) {
            int recording = s->recording; s->recording = 0;
            play_note(w, s->note[s->play_at], s->sound[s->play_at]);
            s->recording = recording; s->play_at++;
        }
        if (s->play_at >= s->count) { s->playing = 0; g_invalidate(w); }
    }
}

static void close(GWindow *w) { stop_all(w); }

const GApp app_piano = {"Piano", "Play, record and save tunes", ICON_PIANO, 594, 300, 594, 300,
                        APP_SINGLE, 0, 0, sizeof(State), open, event, tick, paint, close};
