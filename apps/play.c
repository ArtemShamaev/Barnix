/* play [file.wav]: plays a WAV file through the sound card, or, without a
 * file, the Happy Gouse fanfare (PC speaker when there is no sound card).
 * Any key stops. */
#include "command.h"
#include <barnix_sound.h>

#define RATE 32000
/* Note numbers (MIDI) and lengths in sixteenths; 0 is a rest. */
static const unsigned char fanfare[][2] = {
    {67, 2}, {72, 2}, {76, 2}, {79, 4}, {76, 2}, {79, 6}, {0, 2},
    {77, 2}, {76, 2}, {74, 2}, {72, 2}, {74, 2}, {76, 2}, {72, 8},
};
#define SIXTEENTH_MS 95

static int stopped(void)
{
    unsigned int key = barnix->poll_key();
    return key && key != KEY_MOUSE;
}

static void speaker_tune(void)
{
    for (unsigned int i = 0; i < sizeof(fanfare) / sizeof(fanfare[0]) && !stopped(); i++) {
        unsigned int ms = fanfare[i][1] * SIXTEENTH_MS;
        barnix->speaker(fanfare[i][0] ? bsnd_note_hz(fanfare[i][0]) : 0);
        barnix->sleep_ms(ms - 15);
        barnix->speaker(0);
        barnix->sleep_ms(15);
    }
    barnix->speaker(0);
}

static int wait_for(int voice)
{
    while (bsnd_playing(voice)) {
        if (stopped()) { bsnd_stop(voice); return 0; }
        barnix->sleep_ms(30);
    }
    return 0;
}

int main(int argc, const char *const *argv)
{
    if (argc > 2) return fail("usage: play [file.wav]");
    char device[64];
    int kind = bsnd_device(device, sizeof(device));
    if (!kind) return fail("play needs a Barnix kernel with sound (ABI 11)");
    if (argc == 1) {
        if (kind != BARNIX_SOUND_CARD) { barnix->puts("PC speaker"); speaker_tune(); return 0; }
        unsigned int total = 0;
        for (unsigned int i = 0; i < sizeof(fanfare) / sizeof(fanfare[0]); i++) total += fanfare[i][1];
        unsigned int frames = (total * SIXTEENTH_MS + 600) * (RATE / 1000), bytes = frames * 4;
        short *song = barnix->memory(&bytes), *note = song + frames;
        if (!song || bytes < frames * 4) return fail("not enough memory");
        unsigned int at = 0;
        for (unsigned int i = 0; i < sizeof(fanfare) / sizeof(fanfare[0]); i++) {
            unsigned int ms = fanfare[i][1] * SIXTEENTH_MS;
            if (fanfare[i][0]) {
                BsndTone tone = {BSND_PLUCK, bsnd_note_hz(fanfare[i][0]), 0, ms + 500, 3, 120, 230};
                unsigned int n = bsnd_synth(note, frames - at, RATE, &tone);
                bsnd_mix(song + at, note, n);
            }
            at += ms * (RATE / 1000);
        }
        barnix->puts(device);
        return wait_for(bsnd_play(song, frames, RATE, 256, 0));
    }
    if (kind != BARNIX_SOUND_CARD) return fail("no sound card: WAV files need Intel HD Audio or AC'97");
    int size = barnix->size(argv[1]);
    if (size <= 0) return fail("cannot read the file");
    unsigned int bytes = (unsigned int)size;
    void *data = barnix->memory(&bytes);
    BarnixSound clip;
    if (!data || bytes < (unsigned int)size) return fail("not enough memory");
    if (barnix->read(argv[1], 0, data, (unsigned int)size) != size) return fail("cannot read the file");
    if (bsnd_wav(data, (unsigned int)size, &clip)) return fail("not a PCM WAV file (8 or 16 bit, mono or stereo)");
    int voice = bsnd_play_clip(&clip);
    if (voice < 0) return fail("the sound card refused the clip");
    barnix->puts("Playing; any key stops");
    return wait_for(voice);
}
