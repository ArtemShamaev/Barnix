#ifndef APP_ABI_H
#define APP_ABI_H

#include "mouse.h"
#define BARNIX_APP_ABI 9U
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
     * No nested requests, arguments or in-memory state preservation. */
    int (*launch)(const char *path);

} BarnixAPI;

#endif
