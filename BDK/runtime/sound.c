/* Barnix Sound (BSND): wrappers around the ABI 11 sound calls, a small
 * synthesizer and a WAV reader. */
#include <barnix_sound.h>

static int available(void) { return barnix && barnix->version >= 11; }

int bsnd_device(char *name, unsigned int capacity)
{
    if (!available()) { if (name && capacity) name[0] = 0; return 0; }
    return barnix->sound_device(name, capacity);
}
int bsnd_play_clip(const BarnixSound *clip) { return available() ? barnix->sound_play(clip) : -1; }
int bsnd_play(const short *samples, unsigned int frames, unsigned int rate, int volume, int pan)
{
    BarnixSound clip = {samples, frames, rate, 1, 16, 0, 0, volume, pan};
    return bsnd_play_clip(&clip);
}
void bsnd_stop(int voice) { if (available()) barnix->sound_stop(voice); }
int bsnd_playing(int voice) { return available() ? barnix->sound_playing(voice) : 0; }
void bsnd_volume(int volume) { if (available()) barnix->sound_volume(volume); }
void bsnd_tone(unsigned int hz) { if (available()) barnix->speaker(hz); }
void bsnd_beep(unsigned int hz, unsigned int ms)
{
    if (!available()) return;
    barnix->speaker(hz);
    barnix->sleep_ms(ms);
    barnix->speaker(0);
}

static const short quarter_sine[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793, 12539,
    13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621,
    29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
    32728, 32757, 32767};
/* sin of phase (256 steps per turn). */
static int sine(unsigned int phase)
{
    phase &= 255;
    int value = quarter_sine[phase & 64 ? 64 - (phase & 63) : phase & 63];
    if (phase == 64 || phase == 192) value = 32767;
    return phase & 128 ? -value : value;
}
/* hz / rate as a 32-bit phase step. */
static unsigned int phase_step(unsigned int hz, unsigned int rate)
{
    unsigned int scaled = hz << 16, whole = scaled / rate, rest = scaled % rate;
    return whole << 16 | ((rest << 16) / rate & 0xffff);
}
static unsigned int noise_state = 0x2545f491;
static int noise(void)
{
    noise_state ^= noise_state << 13; noise_state ^= noise_state >> 17; noise_state ^= noise_state << 5;
    return (int)(noise_state & 0xffff) - 32768;
}

unsigned int bsnd_synth(short *out, unsigned int capacity, unsigned int rate, const BsndTone *tone)
{
    if (!out || !tone || !rate || rate > 96000) return 0;
    unsigned int frames = tone->ms * (rate / 10) / 100;
    if (frames > capacity) frames = capacity;
    unsigned int attack = tone->attack_ms * (rate / 10) / 100, release = tone->release_ms * (rate / 10) / 100;
    if (attack > frames) attack = frames;
    if (release > frames - attack) release = frames - attack;
    unsigned int hz = tone->hz < 1 ? 1 : tone->hz > 20000 ? 20000 : tone->hz;
    unsigned int hz_end = tone->hz_end ? (tone->hz_end > 20000 ? 20000 : tone->hz_end) : hz;
    int volume = tone->volume < 0 ? 0 : tone->volume > 256 ? 256 : tone->volume;
    unsigned int phase = 0, held = 0;
    int sample_hold = 0;
    /* Karplus-Strong string: a noise burst circulating through a low-pass loop. */
    static short string[2400];
    unsigned int period = rate / hz, at = 0;
    if (period < 2) period = 2;
    if (period > 2400) period = 2400;
    if (tone->wave == BSND_PLUCK) for (unsigned int i = 0; i < period; i++) string[i] = (short)(noise() / 2);
    /* Pitch in 16.16, slid linearly; no 64-bit division in a freestanding app. */
    unsigned int pitch = hz << 16, step = phase_step(hz, rate);
    int slide = frames && hz_end != hz ? (((int)hz_end - (int)hz) << 16) / (int)frames : 0;
    for (unsigned int i = 0; i < frames; i++) {
        if (slide) { pitch += (unsigned int)slide; if (!(i & 15)) step = phase_step(pitch >> 16 ? pitch >> 16 : 1, rate); }
        unsigned int p = phase >> 24;
        int s;
        switch (tone->wave) {
        case BSND_SQUARE: s = p & 128 ? -20000 : 20000; break;
        case BSND_TRIANGLE: s = p < 128 ? -32767 + (int)p * 512 : 32767 - (int)(p - 128) * 512; break;
        case BSND_SAW: s = (int)p * 256 - 32768; break;
        case BSND_NOISE:
            held += step;
            if (held < step || !i) sample_hold = noise();   /* new value once per period */
            s = sample_hold; break;
        case BSND_ORGAN: s = (sine(p) * 2 + sine(p * 2) + sine(p * 4) / 2) * 2 / 7; break;
        case BSND_PLUCK: {
            unsigned int next = at + 1 == period ? 0 : at + 1;
            s = string[at];
            string[at] = (short)((string[at] + string[next]) * 255 / 512);   /* average, slow decay */
            at = next;
            s *= 2;
            break;
        }
        default: s = sine(p); break;
        }
        phase += step;
        int gain = volume;
        if (i < attack) gain = gain * (int)i / (int)attack;
        else if (i >= frames - release) gain = gain * (int)(frames - i) / (int)release;
        s = s * gain >> 8;
        out[i] = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    return frames;
}

void bsnd_mix(short *a, const short *b, unsigned int frames)
{
    for (unsigned int i = 0; i < frames; i++) {
        int s = a[i] + b[i];
        a[i] = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

unsigned int bsnd_note_hz(int note)
{
    static const unsigned int ratio[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    int from_a = note - 69 + 120, octave = from_a / 12 - 10;   /* 120: keep it positive */
    unsigned int hz = 440U * ratio[from_a % 12];             /* 16.16 */
    hz = octave >= 0 ? hz << octave : hz >> -octave;
    return (hz + 32768) >> 16;
}

static unsigned int le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }
static unsigned int le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int tag(const unsigned char *p, const char *name) { return p[0] == name[0] && p[1] == name[1] && p[2] == name[2] && p[3] == name[3]; }

int bsnd_wav(const void *file, unsigned int size, BarnixSound *clip)
{
    const unsigned char *data = file;
    if (!data || !clip || size < 12 || !tag(data, "RIFF") || !tag(data + 8, "WAVE")) return -1;
    unsigned int channels = 0, rate = 0, bits = 0;
    for (unsigned int at = 12; at + 8 <= size;) {
        unsigned int length = le32(data + at + 4), body = at + 8;
        if (length > size - body) length = size - body;
        if (tag(data + at, "fmt ") && length >= 16) {
            unsigned int format = le16(data + body);
            if (format == 0xfffe && length >= 26) format = le16(data + body + 24);   /* extensible */
            if (format != 1) return -1;
            channels = le16(data + body + 2); rate = le32(data + body + 4); bits = le16(data + body + 14);
        } else if (tag(data + at, "data")) {
            if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate < 4000 || rate > 96000) return -1;
            clip->data = data + body;
            clip->frames = length / (channels * bits / 8);
            clip->rate = rate; clip->channels = (unsigned char)channels; clip->bits = (unsigned char)bits;
            clip->loop = 0; clip->reserved = 0; clip->volume = 256; clip->pan = 0;
            return clip->frames ? 0 : -1;
        }
        at = body + length + (length & 1);
    }
    return -1;
}

void bsnd_ulaw(const unsigned char *in, unsigned int frames, short *out)
{
    for (unsigned int i = 0; i < frames; i++) {
        int code = ~in[i] & 255, magnitude = ((code & 15) << 3 | 0x84) << (code >> 4 & 7);
        out[i] = (short)(code & 0x80 ? 0x84 - magnitude : magnitude - 0x84);
    }
}
