/* Sounds. Music is synthesized with the BDK sound library: the start
 * fanfare, a goodbye arpeggio, a victory phrase and the goose honk for
 * errors. Effects (windows, messages, games) are recorded CC0 clips by Kenney
 * (sounds/LICENSE.txt), stored as mu-law. Without a sound card the PC speaker
 * plays short melodies instead. */
#include "gouse.h"
#include "gouse_sounds.h"

#define RATE 32000
#define CLIP_RATE 22050
int g_sound_kind;
char g_sound_name[64];
static short *music[G_SOUND_COUNT];
static unsigned int music_frames[G_SOUND_COUNT];
static short *effects[GS_COUNT];
static int playing = -1, speaker_left, speaker_at;
static const unsigned short (*speaker_tune)[2];

/* The recorded clip of each sound, or -1 for synthesized music. */
static const signed char clip_of[G_SOUND_COUNT] = {
    [G_SOUND_STARTUP] = -1, [G_SOUND_EXIT] = -1, [G_SOUND_ERROR] = -1, [G_SOUND_WIN] = -1,
    [G_SOUND_DING] = GS_DING, [G_SOUND_QUESTION] = GS_QUESTION, [G_SOUND_OPEN] = GS_OPEN,
    [G_SOUND_CLOSE] = GS_CLOSE, [G_SOUND_MINIMIZE] = GS_MINIMIZE, [G_SOUND_MAXIMIZE] = GS_MAXIMIZE,
    [G_SOUND_REVEAL] = GS_REVEAL, [G_SOUND_FLAG] = GS_FLAG, [G_SOUND_BOOM] = GS_BOOM,
    [G_SOUND_DISC] = GS_DISC, [G_SOUND_SLIDE] = GS_SLIDE,
};

/* hz, ms; 0 Hz is a pause, 0 ms ends the tune. */
static const unsigned short tune_startup[][2] = {{392, 170}, {523, 170}, {659, 170}, {784, 360}, {659, 170}, {784, 560}, {0, 0}};
static const unsigned short tune_ding[][2] = {{1568, 90}, {0, 0}};
static const unsigned short tune_question[][2] = {{880, 70}, {0, 20}, {1175, 90}, {0, 0}};
static const unsigned short tune_exit[][2] = {{784, 90}, {659, 90}, {523, 90}, {392, 200}, {0, 0}};
static const unsigned short tune_error[][2] = {{233, 120}, {0, 40}, {196, 200}, {0, 0}};
static const unsigned short tune_boom[][2] = {{90, 60}, {70, 80}, {55, 140}, {0, 0}};
static const unsigned short tune_click[][2] = {{1200, 12}, {0, 0}};
static const unsigned short tune_win[][2] = {{784, 110}, {988, 110}, {1175, 110}, {1568, 300}, {0, 0}};
static const unsigned short (*const tunes[G_SOUND_COUNT])[2] = {
    [G_SOUND_STARTUP] = tune_startup, [G_SOUND_DING] = tune_ding, [G_SOUND_QUESTION] = tune_question,
    [G_SOUND_EXIT] = tune_exit, [G_SOUND_ERROR] = tune_error, [G_SOUND_BOOM] = tune_boom,
    [G_SOUND_FLAG] = tune_click, [G_SOUND_DISC] = tune_click, [G_SOUND_WIN] = tune_win,
};

/* MIDI notes and lengths in sixteenths (0 is a rest), as in the play command. */
static const unsigned char fanfare[][2] = {
    {67, 2}, {72, 2}, {76, 2}, {79, 4}, {76, 2}, {79, 6}, {0, 2},
    {77, 2}, {76, 2}, {74, 2}, {72, 2}, {74, 2}, {76, 2}, {72, 8},
};
static const unsigned char goodbye[][2] = {{79, 2}, {76, 2}, {72, 2}, {67, 6}};
static const unsigned char victory[][2] = {{67, 1}, {72, 1}, {76, 1}, {79, 3}, {76, 1}, {79, 4}};
#define SIXTEENTH_MS 95

static void lowpass(short *s, unsigned int n, int k)
{
    int y = 0;
    for (unsigned int i = 0; i < n; i++) { y += (s[i] - y) * k >> 8; s[i] = (short)y; }
}
/* Mix one tone into a music clip at ms. */
static void put(int clip, short *scratch, unsigned int at_ms, int wave, unsigned int hz, unsigned int hz_end,
                unsigned int ms, unsigned int release, int volume, int filter)
{
    unsigned int at = at_ms * (RATE / 1000);
    if (!music[clip] || at >= music_frames[clip]) return;
    BsndTone tone = {wave, hz, hz_end, ms, 8, release, volume};
    unsigned int n = bsnd_synth(scratch, music_frames[clip] - at, RATE, &tone);
    if (filter) lowpass(scratch, n, filter);
    bsnd_mix(music[clip] + at, scratch, n);
}
static void melody(int clip, short *scratch, const unsigned char (*notes)[2], int count)
{
    unsigned int at = 0;
    for (int i = 0; i < count; i++) {
        unsigned int ms = notes[i][1] * SIXTEENTH_MS;
        if (notes[i][0]) put(clip, scratch, at, BSND_PLUCK, bsnd_note_hz(notes[i][0]), 0, ms + 500, 120, 230, 0);
        at += ms;
    }
}
/* A goose honk: a nasal sawtooth with its octave below, sliding down. */
static void honk(int clip, short *scratch, unsigned int at, unsigned int hz, unsigned int ms, int volume)
{
    put(clip, scratch, at, BSND_SAW, hz, hz * 5 / 6, ms, ms / 3, volume, 90);
    put(clip, scratch, at, BSND_SQUARE, hz / 2, hz * 5 / 12, ms, ms / 3, volume / 2, 60);
}

void g_sound_init(void)
{
    g_sound_kind = bsnd_device(g_sound_name, sizeof(g_sound_name));
    if (!g_sound_name[0]) g_copy(g_sound_name, "PC speaker", sizeof(g_sound_name));
    if (g_sound_kind != BARNIX_SOUND_CARD) return;
    for (int i = 0; i < GS_COUNT; i++)
        if ((effects[i] = g_alloc(gs_frames[i] * 2))) bsnd_ulaw(gs_data[i], gs_frames[i], effects[i]);
    static const unsigned int lengths_ms[G_SOUND_COUNT] = {
        [G_SOUND_STARTUP] = 46 * SIXTEENTH_MS + 600, [G_SOUND_EXIT] = 12 * SIXTEENTH_MS + 600,
        [G_SOUND_ERROR] = 500, [G_SOUND_WIN] = 11 * SIXTEENTH_MS + 600};
    short *scratch = g_alloc((46 * SIXTEENTH_MS + 600) * (RATE / 1000) * 2);
    if (!scratch) return;
    for (int i = 0; i < G_SOUND_COUNT; i++)
        if (lengths_ms[i]) { music_frames[i] = lengths_ms[i] * (RATE / 1000); music[i] = g_alloc(music_frames[i] * 2); }
    melody(G_SOUND_STARTUP, scratch, fanfare, sizeof(fanfare) / sizeof(fanfare[0]));
    melody(G_SOUND_EXIT, scratch, goodbye, sizeof(goodbye) / sizeof(goodbye[0]));
    melody(G_SOUND_WIN, scratch, victory, sizeof(victory) / sizeof(victory[0]));
    honk(G_SOUND_ERROR, scratch, 0, 440, 140, 150);
    honk(G_SOUND_ERROR, scratch, 180, 370, 220, 150);
}

void g_sound(int which)
{
    if (which < 0 || which >= G_SOUND_COUNT || !g_settings.volume) return;
    if (which < G_SOUND_REVEAL && !g_settings.sounds) return;   /* game sounds stay on */
    if (g_sound_kind == BARNIX_SOUND_CARD) {
        int clip = clip_of[which];
        if (clip >= 0 && effects[clip]) playing = bsnd_play(effects[clip], gs_frames[clip], CLIP_RATE, 256, 0);
        else if (clip < 0 && music[which]) playing = bsnd_play(music[which], music_frames[which], RATE, 256, 0);
    } else if (g_sound_kind == BARNIX_SOUND_SPEAKER && tunes[which]) {
        speaker_tune = tunes[which]; speaker_at = 0;
        speaker_left = speaker_tune[0][1];
        bsnd_tone(speaker_tune[0][0]);
    }
}

void g_sound_tick(int dt)
{
    if (!speaker_tune) return;
    speaker_left -= dt;
    while (speaker_tune && speaker_left <= 0) {
        speaker_at++;
        if (!speaker_tune[speaker_at][1]) { speaker_tune = 0; bsnd_tone(0); break; }
        speaker_left += speaker_tune[speaker_at][1];
        bsnd_tone(speaker_tune[speaker_at][0]);
    }
}

int g_sound_busy(void) { return speaker_tune || (playing >= 0 && bsnd_playing(playing)); }
