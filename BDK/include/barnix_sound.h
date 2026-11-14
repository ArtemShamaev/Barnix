#ifndef BARNIX_SOUND_H
#define BARNIX_SOUND_H
#include <barnix_api.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix Sound (BSND), ABI 11. The kernel mixes up to 16 clips at 48 kHz
 * into Intel HD Audio or AC'97; without a sound card only the PC speaker
 * (tones) is available. Clips play from the program's memory, so keep the
 * samples alive (static or barnix->memory) while they sound. No FPU. */

/* BARNIX_SOUND_CARD, BARNIX_SOUND_SPEAKER, or 0 on kernels before ABI 11. */
int bsnd_device(char *name, unsigned int capacity);
/* Mono signed 16-bit samples. volume 0..256, pan -256..256; returns a voice. */
int bsnd_play(const short *samples, unsigned int frames, unsigned int rate, int volume, int pan);
int bsnd_play_clip(const BarnixSound *clip);
void bsnd_stop(int voice);          /* -1: every clip */
int bsnd_playing(int voice);
void bsnd_volume(int volume);       /* master 0..256 */

/* PC speaker: a tone until bsnd_tone(0), or a blocking beep. */
void bsnd_tone(unsigned int hz);
void bsnd_beep(unsigned int hz, unsigned int ms);

/* Synthesis of one note into out (mono, signed 16-bit). The pitch slides
 * from hz to hz_end (0 = no slide); attack and release are linear ramps.
 * Returns the frames written (ms * rate / 1000, at most capacity). */
enum { BSND_SQUARE, BSND_TRIANGLE, BSND_SAW, BSND_SINE, BSND_NOISE, BSND_ORGAN, BSND_PLUCK };
typedef struct {
    int wave;
    unsigned int hz, hz_end, ms, attack_ms, release_ms;
    int volume;                     /* 0..256 */
} BsndTone;
unsigned int bsnd_synth(short *out, unsigned int capacity, unsigned int rate, const BsndTone *tone);
/* Adds b into a with saturation (layering several notes into one clip). */
void bsnd_mix(short *a, const short *b, unsigned int frames);
/* MIDI note number to Hz (69 = A4 = 440 Hz), rounded. */
unsigned int bsnd_note_hz(int note);

/* A RIFF WAVE file in memory (PCM, 8 or 16 bit, mono or stereo):
 * fills clip (volume 256, centred) and returns 0, or -1. */
int bsnd_wav(const void *file, unsigned int size, BarnixSound *clip);
/* G.711 mu-law bytes (scripts/sound_assets.py writes them) to 16-bit samples. */
void bsnd_ulaw(const unsigned char *in, unsigned int frames, short *out);
#ifdef __cplusplus
}
#endif
#endif
