#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "system_sound.h"
/* Host input tests do not link the hardware USB driver. */
extern int usb_mouse_poll(void) __attribute__((weak));
extern int usb_xhci_poll(void) __attribute__((weak));
extern int usb_xhci_poll_reports(void) __attribute__((weak));

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
    if (key == 0x1d) {   /* left Ctrl, or right Ctrl (extended) */
        bit = ext ? 2 : 1;
        if (released) control &= ~bit; else control |= bit;
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
        /* Ctrl+letter is a control code in every layout: Ctrl+C, Ctrl+V. */
        if (control && normal_map[key] >= 'a' && normal_map[key] <= 'z') return normal_map[key] - 'a' + 1;
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
        if (c >= 'a' && c <= 'z') return caps ^ shift ? c - 32 : c;
        return shift ? (unsigned int)(unsigned char)shift_map[key] :
                       (unsigned int)(unsigned char)c;
    }
    if (shifts && alts && !chord) { russian = !russian; chord = 1; }
    if (!shifts && !alts) chord = 0;
    return 0;
}
/* Keyboard mouse, for machines whose mouse or touchpad Barnix cannot drive:
 * Ctrl+Alt+M switches it. The arrows move the pointer (faster while held),
 * Space or Enter is the left button, Shift+Space or Shift+Enter the right
 * one, PageUp/PageDown the wheel. Those keys stop reaching programs while
 * it is on; everything else types as usual. It is pointer source 95. */
void system_sound(int which) __attribute__((weak));
#define KEYMOUSE_SOURCE 95
static int keymouse_on;
static unsigned int keymouse_arrows, keymouse_buttons;   /* bits: up, down, left, right */
static unsigned int keymouse_held_ms, keymouse_rest_x, keymouse_rest_y;
static unsigned long long keymouse_last, keymouse_pressed_at;
static int keymouse_release_pending;
static unsigned int keymouse_cycles_per_ms;
static unsigned long long keymouse_tsc(void)
{
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (unsigned long long)hi << 32 | lo;
}
/* TSC cycles per millisecond (timer.c). */
static void keymouse_calibrate(void)
{
    keymouse_cycles_per_ms = timer_tsc_per_ms();
    if (keymouse_cycles_per_ms < 1000) keymouse_cycles_per_ms = 1000000;   /* no TSC: assume 1 GHz */
}
int keyboard_mouse_enabled(void) { return keymouse_on; }
static void keymouse_switch(void)
{
    keymouse_on = !keymouse_on;
    keymouse_arrows = keymouse_buttons = 0;
    if (keymouse_on && !keymouse_cycles_per_ms) keymouse_calibrate();
    keymouse_last = keymouse_tsc();
    mouse_source(KEYMOUSE_SOURCE, keymouse_on, 0, 0, 0, 0, 0);
    if (system_sound) system_sound(keymouse_on ? SYSTEM_SOUND_KEYMOUSE_ON : SYSTEM_SOUND_KEYMOUSE_OFF);
}
/* Moves the pointer for the time the arrows have been held; 1 when it moved. */
static int keymouse_tick(void)
{
    if (!keymouse_on) return 0;
    unsigned long long now = keymouse_tsc(), elapsed = now - keymouse_last;
    /* A click lasts at least 60 ms, so programs that look once a frame see it. */
    if (keymouse_release_pending && now - keymouse_pressed_at >= (unsigned long long)keymouse_cycles_per_ms * 60) {
        keymouse_release_pending = 0;
        keymouse_buttons &= ~(BARNIX_MOUSE_LEFT | BARNIX_MOUSE_RIGHT);
        mouse_source(KEYMOUSE_SOURCE, 1, 0, 0, keymouse_buttons, 0, 0);
        return 1;
    }
    /* No 64-bit division in the kernel: clamp to 100 ms first, then divide in 32 bits. */
    unsigned long long limit = (unsigned long long)keymouse_cycles_per_ms * 100;
    if (elapsed > limit) { keymouse_last = now - limit; elapsed = limit; }
    unsigned int ms = (unsigned int)elapsed / keymouse_cycles_per_ms;
    if (!ms) return 0;
    keymouse_last += (unsigned long long)ms * keymouse_cycles_per_ms;
    if (!keymouse_arrows) { keymouse_held_ms = keymouse_rest_x = keymouse_rest_y = 0; return 0; }
    keymouse_held_ms += ms;
    /* 90 logical pixels a second at first, up to 600 after 1.2 s. */
    unsigned int speed = 90 + 510 * (keymouse_held_ms > 1200 ? 1200 : keymouse_held_ms) / 1200;
    int sx = (keymouse_arrows & 8 ? 1 : 0) - (keymouse_arrows & 4 ? 1 : 0);
    int sy = (keymouse_arrows & 2 ? 1 : 0) - (keymouse_arrows & 1 ? 1 : 0);
    keymouse_rest_x += sx ? speed * ms : 0; keymouse_rest_y += sy ? speed * ms : 0;
    int dx = sx * (int)(keymouse_rest_x / 1000), dy = sy * (int)(keymouse_rest_y / 1000);
    keymouse_rest_x %= 1000; keymouse_rest_y %= 1000;
    if (!dx && !dy) return 0;
    return mouse_source(KEYMOUSE_SOURCE, 1, dx, dy, keymouse_buttons, 0, 0);
}
/* 1 when the key belongs to the keyboard mouse and must not reach programs. */
static int keymouse_key(unsigned int code, int pressed)
{
    unsigned int bit = 0;
    if (code == 0xc8 || code == 0x48) bit = 1;          /* up (also the keypad's 8) */
    else if (code == 0xd0 || code == 0x50) bit = 2;     /* down */
    else if (code == 0xcb || code == 0x4b) bit = 4;     /* left */
    else if (code == 0xcd || code == 0x4d) bit = 8;     /* right */
    if (bit) {
        keymouse_tick();   /* movement so far, under the old set of arrows */
        if (pressed && !(keymouse_arrows & bit)) {
            keymouse_arrows |= bit;
            /* A tap nudges by two pixels, for precise aiming. */
            int dx = bit == 8 ? 2 : bit == 4 ? -2 : 0, dy = bit == 2 ? 2 : bit == 1 ? -2 : 0;
            mouse_source(KEYMOUSE_SOURCE, 1, dx, dy, keymouse_buttons, 0, 0);
        } else if (!pressed) keymouse_arrows &= ~bit;
        if (!keymouse_arrows) keymouse_tick();
        return 1;
    }
    if (code == 0x39 || code == 0x1c || code == 0x9c) {  /* Space, Enter, keypad Enter */
        unsigned int button = shifts ? BARNIX_MOUSE_RIGHT : BARNIX_MOUSE_LEFT;
        if (pressed) {
            if (!(keymouse_buttons & button)) keymouse_pressed_at = keymouse_tsc();
            keymouse_buttons |= button;
            keymouse_release_pending = 0;
        } else if (keymouse_tsc() - keymouse_pressed_at < (unsigned long long)keymouse_cycles_per_ms * 60) {
            keymouse_release_pending = 1;   /* released by keymouse_tick */
            return 1;
        } else keymouse_buttons &= ~(BARNIX_MOUSE_LEFT | BARNIX_MOUSE_RIGHT);
        mouse_source(KEYMOUSE_SOURCE, 1, 0, 0, keymouse_buttons, 0, 0);
        return 1;
    }
    if (code == 0xc9 || code == 0xd1) {                 /* PageUp, PageDown */
        if (pressed) mouse_source(KEYMOUSE_SOURCE, 1, 0, 0, keymouse_buttons, code == 0xc9 ? -1 : 1, 0);
        return 1;
    }
    return 0;
}
/* Every PS/2 keyboard byte passes here, so press and release events are
 * queued no matter which API (getch, poll_key or key_event) read the port. */
static BarnixKeyEvent key_queue[64];
static unsigned int key_head, key_tail, key_skip;
static unsigned char key_held[256];
static int key_extended;
static unsigned int keyboard_byte(unsigned char scan)
{
    unsigned int key = keyboard_feed(scan);
    int ext = key_extended; key_extended = scan == 0xe0;
    if (key_skip) { key_skip--; return key; }
    if (scan == 0xe1) { key_skip = 5; return key; } /* Pause: E1 1D 45 E1 9D C5 */
    if (scan == 0xe0 || scan == 0xfa || scan == 0xfe || scan == 0x00 || scan == 0xff) return key;
    unsigned int code = (scan & 0x7f) | (ext ? 0x80 : 0);
    if (code == 0xaa || code == 0xb6) return key; /* E0-prefixed fake shifts */
    if (code == 0x32 && !(scan & 0x80) && control && alts) {   /* Ctrl+Alt+M */
        if (!key_held[code]) keymouse_switch();
        key_held[code] = 1;
        return 0;
    }
    if (keymouse_on && keymouse_key(code, !(scan & 0x80))) { key_held[code] = !(scan & 0x80); return 0; }
    int pressed = !(scan & 0x80), repeat = pressed && key_held[code];
    key_held[code] = pressed;
    if (key_tail - key_head == 64) { if (repeat) return key; key_head++; }
    BarnixKeyEvent *event = &key_queue[key_tail++ & 63];
    event->code = code; event->pressed = pressed; event->repeat = repeat; event->key = key;
    return key;
}
void keyboard_events_reset(void) { key_head = key_tail; }
void audio_update(void) __attribute__((weak));
int keyboard_key_event(BarnixKeyEvent *event)
{
    if (audio_update) audio_update();
    keymouse_tick();
    if (key_head == key_tail) {
        /* Per-frame path for games: hot-plug handling stays in getch/poll_key. */
        if (usb_xhci_poll_reports) usb_xhci_poll_reports();
        if (usb_mouse_poll) usb_mouse_poll();
        for (int i = 0; i < 32; i++) {
            unsigned char status = inb(0x64);
            if (!(status & 1)) break;
            unsigned char scan = inb(0x60);
            if (status & 0x20) mouse_feed(scan); else keyboard_byte(scan);
        }
    }
    if (key_head == key_tail || !event) return 0;
    *event = key_queue[key_head++ & 63];
    return 1;
}
unsigned int keyboard_event(void)
{
    for (;;) {
        if (audio_update) audio_update();   /* sounds keep playing at the prompt */
        if (keymouse_tick()) return KEY_MOUSE;
        if(usb_xhci_poll && usb_xhci_poll())return KEY_MOUSE;
        if(usb_mouse_poll && usb_mouse_poll())return KEY_MOUSE;
        unsigned char status = inb(0x64);
        if (status & 1) {
            unsigned char scan = inb(0x60);
            if (status & 0x20) { if(mouse_feed(scan))return KEY_MOUSE;continue; }
            unsigned int c = keyboard_byte(scan);
            if (c) return c;
        }
    }
}
unsigned int keyboard_poll(void) {
    if(audio_update)audio_update();
    if(keymouse_tick())return KEY_MOUSE;
    if(usb_xhci_poll && usb_xhci_poll())return KEY_MOUSE;
        if(usb_mouse_poll && usb_mouse_poll())return KEY_MOUSE;
    for(int i=0;i<32;i++) {
        unsigned char status=inb(0x64);
        if(!(status&1))return 0;
        unsigned char scan=inb(0x60);
        if(status&0x20) { if(mouse_feed(scan))return KEY_MOUSE; }
        else { unsigned int key=keyboard_byte(scan);if(key)return key; }
    }
    return 0;
}
/* Sound keeps playing while a program sleeps; a speaker tone needs PIT
 * channel 2, so audio_sleep times that case with the TSC instead. */
int audio_sleep(unsigned int milliseconds) __attribute__((weak));
void keyboard_sleep(unsigned int milliseconds) {
    if(milliseconds>1000)milliseconds=1000;
    if(audio_sleep&&audio_sleep(milliseconds))return;
    while(milliseconds) {
        if(audio_update)audio_update();
        unsigned int part=milliseconds>20?20:milliseconds;milliseconds-=part;
        timer_delay_ms(part);
    }
}

unsigned int getch(void) { unsigned int key;do{key=keyboard_event();}while(key==KEY_MOUSE);return key; }
