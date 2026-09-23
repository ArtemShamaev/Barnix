#include "keyboard.h"

static unsigned int shifts, alts;
static int caps, caps_down, extended, chord, russian, control;
void keyboard_set_layout(int ru) { russian = !!ru; }
int keyboard_layout(void) { return russian; }
void keyboard_reset(void) { shifts = alts = 0; caps = caps_down = extended = chord = russian = control = 0; }

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static const char normal_map[128] =
{
    [0x02]='1',
    [0x03]='2',
    [0x04]='3',
    [0x05]='4',
    [0x06]='5',
    [0x07]='6',
    [0x08]='7',
    [0x09]='8',
    [0x0A]='9',
    [0x0B]='0',

    [0x0C]='-',
    [0x0D]='=',

    [0x0E]='\b',
    [0x0F]='\t',

    [0x10]='q',
    [0x11]='w',
    [0x12]='e',
    [0x13]='r',
    [0x14]='t',
    [0x15]='y',
    [0x16]='u',
    [0x17]='i',
    [0x18]='o',
    [0x19]='p',

    [0x1A]='[',
    [0x1B]=']',

    [0x1C]='\n',

    [0x1E]='a',
    [0x1F]='s',
    [0x20]='d',
    [0x21]='f',
    [0x22]='g',
    [0x23]='h',
    [0x24]='j',
    [0x25]='k',
    [0x26]='l',

    [0x27]=';',
    [0x28]='\'',

    [0x29]='`',

    [0x2B]='\\',

    [0x2C]='z',
    [0x2D]='x',
    [0x2E]='c',
    [0x2F]='v',
    [0x30]='b',
    [0x31]='n',
    [0x32]='m',

    [0x33]=',',
    [0x34]='.',
    [0x35]='/',

    [0x39]=' '
};

static const char shift_map[128] =
{
    [0x02]='!',
    [0x03]='@',
    [0x04]='#',
    [0x05]='$',
    [0x06]='%',
    [0x07]='^',
    [0x08]='&',
    [0x09]='*',
    [0x0A]='(',
    [0x0B]=')',

    [0x0C]='_',
    [0x0D]='+',

    [0x0E]='\b',
    [0x0F]='\t',

    [0x10]='Q',
    [0x11]='W',
    [0x12]='E',
    [0x13]='R',
    [0x14]='T',
    [0x15]='Y',
    [0x16]='U',
    [0x17]='I',
    [0x18]='O',
    [0x19]='P',

    [0x1A]='{',
    [0x1B]='}',

    [0x1C]='\n',

    [0x1E]='A',
    [0x1F]='S',
    [0x20]='D',
    [0x21]='F',
    [0x22]='G',
    [0x23]='H',
    [0x24]='J',
    [0x25]='K',
    [0x26]='L',

    [0x27]=':',
    [0x28]='"',

    [0x29]='~',

    [0x2B]='|',

    [0x2C]='Z',
    [0x2D]='X',
    [0x2E]='C',
    [0x2F]='V',
    [0x30]='B',
    [0x31]='N',
    [0x32]='M',

    [0x33]='<',
    [0x34]='>',
    [0x35]='?',

    [0x39]=' '
};


static const unsigned int ru_map[128] = {
    [0x10]=0x439, [0x11]=0x446, [0x12]=0x443, [0x13]=0x43a,
    [0x14]=0x435, [0x15]=0x43d, [0x16]=0x433, [0x17]=0x448,
    [0x18]=0x449, [0x19]=0x437, [0x1a]=0x445, [0x1b]=0x44a,
    [0x1e]=0x444, [0x1f]=0x44b, [0x20]=0x432, [0x21]=0x430,
    [0x22]=0x43f, [0x23]=0x440, [0x24]=0x43e, [0x25]=0x43b,
    [0x26]=0x434, [0x27]=0x436, [0x28]=0x44d, [0x29]=0x451,
    [0x2c]=0x44f, [0x2d]=0x447, [0x2e]=0x441, [0x2f]=0x43c,
    [0x30]=0x438, [0x31]=0x442, [0x32]=0x44c, [0x33]=0x431,
    [0x34]=0x44e
};
/* Pure set-1 decoder, also used by host tests. A chord toggles only once
 * until both modifier families have been released (including key repeats). */
unsigned int keyboard_feed(unsigned char scan)
{
    if (scan == 0xe0) { extended = 1; return 0; }
    int ext = extended; extended = 0;
    int released = scan & 0x80;
    unsigned int key = scan & 0x7f, bit;
    if (ext && !released) {
        if (key == 0x48) return KEY_UP;
        if (key == 0x50) return KEY_DOWN;
        if (key == 0x4b) return KEY_LEFT;
        if (key == 0x4d) return KEY_RIGHT;
        if (key == 0x47) return KEY_HOME;
        if (key == 0x4f) return KEY_END;
        if (key == 0x53) return KEY_DELETE;
    }
    if (!ext && key == 1 && !released) return KEY_ESCAPE;
    if (!ext && key == 0x1d) {
        control = !released;
    } else if (!ext && (key == 0x2a || key == 0x36)) {
        bit = key == 0x2a ? 1 : 2;
        if (released) shifts &= ~bit; else shifts |= bit;
    } else if (key == 0x38) {
        bit = ext ? 2 : 1;
        if (released) alts &= ~bit; else alts |= bit;
    } else {
        if (!ext && key == 0x3a) {
            if (!released && !caps_down) caps = !caps;
            caps_down = !released; return 0;
        }
        if (released || ext || alts) return 0;
        int shift = shifts != 0;
        unsigned int c = russian ? ru_map[key] : 0;
        if (c) return caps ^ shift ? (c == 0x451 ? 0x401 : c - 32) : c;
        if (russian) {
            if (key == 0x35) return shift ? ',' : '.';
            if (key == 0x2b) return shift ? '/' : '\\';
            if (shift) {
                if (key == 0x03) return '"';
                if (key == 0x04) return 0x2116;
                if (key == 0x05) return ';';
                if (key == 0x07) return ':';
                if (key == 0x08) return '?';
            }
        }
        c = normal_map[key];
        if (control && c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'a' && c <= 'z') return caps ^ shift ? c - 32 : c;
        return shift ? (unsigned int)(unsigned char)shift_map[key] :
                       (unsigned int)(unsigned char)c;
    }
    if (shifts && alts && !chord) { russian = !russian; chord = 1; }
    if (!shifts && !alts) chord = 0;
    return 0;
}
unsigned int getch(void)
{
    for (;;) {
        unsigned char status = inb(0x64);
        if (status & 1) {
            unsigned char scan = inb(0x60);
            if (status & 0x20) continue; /* Ignore PS/2 mouse bytes. */
            unsigned int c = keyboard_feed(scan);
            if (c) return c;
        }
    }
}
