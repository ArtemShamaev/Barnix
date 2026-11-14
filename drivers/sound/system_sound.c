/* The start chime is synthesized and the error tone is a CC0 clip by Kenney
 * (sounds/LICENSE.txt); both are stored as mu-law and decoded on first use.
 * Without a sound card the PC speaker beeps. */
#include "system_sound.h"
#include "audio.h"
#include "keyboard.h"
#include "system_sounds.h"

static short pcm[SYS_TOTAL_FRAMES];
static short *clips[SYS_COUNT];
static int enabled = 1;

static void decode(void)
{
    short *at = pcm;
    for (int i = 0; i < SYS_COUNT; i++) {
        clips[i] = at;
        for (unsigned int n = 0; n < sys_frames[i]; n++) {
            int code = ~sys_data[i][n] & 255, magnitude = ((code & 15) << 3 | 0x84) << (code >> 4 & 7);
            at[n] = (short)(code & 0x80 ? 0x84 - magnitude : magnitude - 0x84);
        }
        at += sys_frames[i];
    }
}

/* Keyboard mouse on/off: two short notes, rising or falling. */
#define NOTE_FRAMES 1323   /* 60 ms at 22050 Hz */
static short switch_pcm[2][2 * NOTE_FRAMES];
static void switch_sound(int on)
{
    static const unsigned short hz[2][2] = {{660, 990}, {990, 660}};
    int k = on ? 0 : 1;
    if (audio_device(0, 0) == BARNIX_SOUND_CARD) {
        short *out = switch_pcm[k];
        if (!out[10]) {
            for (int note = 0; note < 2; note++) {
                unsigned int period = 22050U / hz[k][note];
                for (unsigned int n = 0; n < NOTE_FRAMES; n++) {
                    int level = 5000 * (int)(NOTE_FRAMES - n) / NOTE_FRAMES + 1000;   /* fading square wave */
                    out[note * NOTE_FRAMES + n] = (short)(n % period < period / 2 ? level : -level);
                }
            }
        }
        BarnixSound sound = {out, 2 * NOTE_FRAMES, 22050, 1, 16, 0, 0, 256, 0};
        audio_play(&sound);
        return;
    }
    for (int note = 0; note < 2; note++) { audio_speaker(hz[k][note]); keyboard_sleep(60); }
    audio_speaker(0);
}
void system_sound(int which)
{
    if (!enabled) return;
    if (which == SYSTEM_SOUND_KEYMOUSE_ON || which == SYSTEM_SOUND_KEYMOUSE_OFF) { switch_sound(which == SYSTEM_SOUND_KEYMOUSE_ON); return; }
    if (audio_device(0, 0) == BARNIX_SOUND_CARD) {
        if (!clips[0]) decode();
        int clip = which == SYSTEM_SOUND_START ? SYS_START : SYS_ERROR;
        BarnixSound sound = {clips[clip], sys_frames[clip], 22050, 1, 16, 0, 0, 256, 0};
        audio_play(&sound);
        return;
    }
    static const unsigned short start[][2] = {{523, 70}, {659, 70}, {784, 70}, {1047, 140}};
    static const unsigned short error[][2] = {{330, 90}};
    const unsigned short (*tune)[2] = which == SYSTEM_SOUND_START ? start : error;
    int count = which == SYSTEM_SOUND_START ? 4 : 1;
    for (int i = 0; i < count; i++) { audio_speaker(tune[i][0]); keyboard_sleep(tune[i][1]); }
    audio_speaker(0);
}

void system_sound_enable(int on) { enabled = !!on; }
int system_sound_enabled(void) { return enabled; }
