#ifndef APP_ABI_H
#define APP_ABI_H

#define BARNIX_APP_ABI 5U
#define APP_LOAD_MIN 0x01000000U
#define APP_STACK_TOP 0x01400000U
#define APP_STACK_SIZE 0x00010000U
#define APP_LOAD_END (APP_STACK_TOP - APP_STACK_SIZE)

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
} BarnixAPI;

#endif
