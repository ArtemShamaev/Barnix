#ifndef AUDIO_H
#define AUDIO_H
/* A PCM clip for sound_play. The samples stay in the program's memory and
 * must remain valid while the clip plays. */
typedef struct {
    const void *data;        /* interleaved when stereo */
    unsigned int frames;     /* samples per channel */
    unsigned int rate;       /* 4000..96000 Hz */
    unsigned char channels;  /* 1 or 2 */
    unsigned char bits;      /* 8 (unsigned) or 16 (signed) */
    unsigned char loop;      /* repeat until sound_stop */
    unsigned char reserved;
    int volume;              /* 0..256 */
    int pan;                 /* -256 left .. 0 centre .. 256 right */
} BarnixSound;

#define BARNIX_SOUND_NONE 0
#define BARNIX_SOUND_SPEAKER 1   /* only the PC speaker: tones, no PCM */
#define BARNIX_SOUND_CARD 2

void audio_init(void);
/* Refills the output buffer; called from frame and input polling. */
void audio_update(void);
/* Stops every clip and the speaker (program exit). */
void audio_reset(void);
/* keyboard_sleep while a speaker tone sounds: 1 when handled. */
int audio_sleep(unsigned int milliseconds);
int audio_device(char *name, unsigned int capacity);
void audio_speaker(unsigned int hz);
int audio_play(const BarnixSound *sound);
void audio_stop(int voice);
int audio_playing(int voice);
void audio_volume(int volume);
#endif
