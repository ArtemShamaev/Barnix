#ifndef APP_ABI_H
#define APP_ABI_H

#include "mouse.h"
#include "keyboard.h"
#include "audio.h"
#include "netinfo.h"
#define BARNIX_APP_ABI 14U
#define APP_LOAD_MIN 0x01000000U
#define APP_STACK_TOP 0x01400000U
#define APP_STACK_SIZE 0x00010000U
#define APP_LOAD_END (APP_STACK_TOP - APP_STACK_SIZE)

typedef struct {
    char name[256];
    unsigned int kind; /* 4: directory, 8: regular file, 0: other */
} BarnixDirEntry;

/* i386 cdecl ABI. Applications run synchronously, one at a time. */
typedef struct {
    unsigned int version;
    void (*puts)(const char *text);
    int (*size)(const char *name);
    int (*read)(const char *name, unsigned int offset, void *data, unsigned int size);
    int (*write)(const char *name, const char *data, int size);
    int (*append)(const char *name, const char *data, int size);
    void (*print)(int color, const char *text);
    void (*println)(int color, const char *text);
    void (*clear)(void);
    void (*panic)(const char *text);
    void (*ls)(void);
    void (*pwd)(void);
    void (*df)(void);
    void (*disk_info)(void);
    void (*devices)(void);
    void (*mount_info)(void);
    int (*touch)(const char *name);
    int (*rm)(const char *name);
    int (*mkdir)(const char *name);
    int (*cd)(const char *name);
    int (*rmdir)(const char *name);
    int (*stat)(const char *name);
    int (*cp)(const char *source, const char *destination);
    int (*mv)(const char *source, const char *destination);
    int (*mount)(const char *device, const char *mountpoint);
    int (*unmount)(void);
    int (*sync)(void);
    int (*init)(void);
    const char *(*translate)(const char *text);
    unsigned int (*getch)(void);
    void (*goto_xy)(int x, int y);
    int (*net_connect_wifi)(const char *name, const char *password);
    int (*net_connect_cable)(void);
    int (*net_scan)(char *out, unsigned int size);
    int (*net_ping)(unsigned int *milliseconds);
    int (*net_download)(const char *url, const char *destination);
    int (*net_state)(void);
    int (*format)(const char *device);
    int (*unmount_path)(const char *path);
    void (*lsblk)(void);
    unsigned int (*poll_key)(void);
    void (*sleep_ms)(unsigned int milliseconds);
    const char *(*current_user)(void);
    int (*chmod)(const char *path, const char *spec, const char *password);
    int (*theme)(const char *name);
    int (*partition)(int argc, const char *const *argv);
    /* Snapshot after poll_key(); KEY_MOUSE signals movement/button changes.
     * Coordinates: 0..639, 0..399; buttons: BARNIX_MOUSE_* bitmask.
     * Does not consume keyboard input; NULL is allowed. */
    void (*mouse)(BarnixMouse *state);
    void (*screen)(const unsigned short *cells);
    /* ABI 8 extensions: 640x400 indexed EGA pixels; NULL leaves graphics mode.
     * Returns -1 on text-only firmware, 0 on success. */
    int (*graphics)(const unsigned char *pixels);
    unsigned char (*glyph)(unsigned int code, unsigned int row);
    void (*pointer)(BarnixPointer *state);
    /* ABI 9: directory iteration returns 1 / 0 (end) / negative error.
     * Initialize position to zero. Paths follow the current working directory. */
    int (*getcwd)(char *out, unsigned int capacity);
    int (*directory)(const char *path, unsigned int *position, BarnixDirEntry *entry);
    /* Queue one program, then return 0 from main. The kernel runs it, waits
     * for a key, restores cwd, and restarts this launcher from its entry point.
     * No nested requests or in-memory state preservation. Since ABI 14 path
     * may be followed by arguments: "program arg \"arg with spaces\"". */
    int (*launch)(const char *path);
    /* ABI 10: true-color output and raw input for games and graphic libraries.
     * present() takes an XRGB8888 frame (0x00RRGGBB), enlarges it by the
     * largest integer factor that fits the display (or reduces it by a box
     * filter) and centers it. UEFI draws to the GOP framebuffer; Legacy BIOS
     * switches to Bochs/QEMU VBE or VGA 320x200x256 and restores text mode on
     * present(NULL, 0, 0) or program exit. Returns -1 without a display. */
    int (*present)(const unsigned int *pixels, unsigned int width, unsigned int height);
    int (*display)(unsigned int *width, unsigned int *height);
    /* Key presses, auto-repeats and releases, oldest first: 1, or 0 when empty.
     * Also polls mice, so pointer() and motion() stay current. */
    int (*key_event)(BarnixKeyEvent *event);
    /* Mouse movement since the previous call, not clamped to the screen. */
    void (*motion)(int *dx, int *dy);
    /* Zeroed RAM outside the 4 MiB program arena, valid until exit: *bytes
     * in = wanted (0 = everything), out = granted; NULL when there is none. */
    void *(*memory)(unsigned int *bytes);
    /* ABI 11: processor cores and sound. parallel() calls job(argument, i)
     * for i in 0..count-1 on every core and returns when all are done; jobs
     * may only compute (no Barnix calls). cpus() includes the calling core. */
    unsigned int (*cpus)(void);
    void (*parallel)(void (*job)(void *argument, unsigned int index), void *argument, unsigned int count);
    /* BARNIX_SOUND_CARD (PCM through Intel HD Audio or AC'97) or
     * BARNIX_SOUND_SPEAKER (tones only); name gets the device description. */
    int (*sound_device)(char *name, unsigned int capacity);
    void (*speaker)(unsigned int hz);   /* PC speaker tone, 0 = off */
    /* Mixed at 48 kHz, up to 16 at once. Returns a voice id or -1. */
    int (*sound_play)(const BarnixSound *sound);
    void (*sound_stop)(int voice);      /* -1 stops every clip */
    int (*sound_playing)(int voice);
    void (*sound_volume)(int volume);   /* master, 0..256; reset at exit */
    /* ABI 12: TCP/IP. net_open connects to a name or dotted address (DHCP
     * and DNS happen as needed) and returns a handle or BARNIX_NET_*;
     * net_recv returns bytes, 0 when the peer closed, -2 on timeout, -1 on
     * error. Connections still open are dropped at exit. BDK barnix_net.h
     * adds HTTP and TLS. Addresses are in host byte order. */
    int (*net_open)(const char *host, unsigned int port);
    int (*net_send)(int handle, const void *data, unsigned int length);
    int (*net_recv)(int handle, void *data, unsigned int capacity, unsigned int timeout_ms);
    void (*net_close)(int handle);
    int (*net_resolve)(const char *host, unsigned int *address);
    int (*net_info)(BarnixNetInfo *info);
    int (*net_ping_host)(unsigned int address, unsigned int *milliseconds);
    void (*random)(void *out, unsigned int length);   /* unpredictable bytes */
    /* ABI 13: runs the program at path inside this one, as a shell would,
     * and collects what it prints into output (UTF-8, '\f' for clear).
     * swap: (APP_STACK_TOP - APP_LOAD_MIN) bytes of this program's memory()
     * where the kernel keeps this program while the other one runs. A
     * program that waits for a key or draws takes the whole screen until it
     * ends. Returns the output length or a negative error; status gets the
     * exit status. Nested programs cannot run() or launch(). */
    int (*run)(const char *path, int argc, const char *const *argv, char *output, unsigned int capacity, void *swap, int *status);
    /* ABI 14: the system clipboard, shared by the shell, macro, Happy Gouse
     * and every program: UTF-8 text up to 64 KiB. clipboard_get copies at
     * most capacity - 1 bytes plus a terminating zero and returns the whole
     * length; clipboard_serial changes whenever someone copies. */
    int (*clipboard_set)(const char *text, unsigned int length);
    int (*clipboard_get)(char *out, unsigned int capacity);
    unsigned int (*clipboard_serial)(void);
    /* Permission bits (0644, 0755...) of a regular file; root only. */
    int (*file_mode)(const char *path, unsigned int mode);
    /* Where the next run() shows a program that reads the keyboard or
     * draws: a rectangle of the frame this program presented last. The
     * program's text console (80x25 cells of 8x16 pixels) or picture is
     * drawn there and the rest of the frame stays; it returns without
     * "Press a key". Applies to one run(); w or h 0: the whole screen. */
    void (*run_area)(int x, int y, int w, int h);
    /* The next run() starts its program as root (sudo) when password is
     * root's, or this program already runs as root: 0, else -13. */
    int (*sudo)(const char *password);
    /* Free KiB and free file slots (inodes) of the disk that holds path. */
    int (*disk_space)(const char *path, unsigned int *free_kib, unsigned int *free_files);

} BarnixAPI;

#endif
