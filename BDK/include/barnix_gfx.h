#ifndef BARNIX_GFX_H
#define BARNIX_GFX_H
#include <barnix_api.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix Graphics (BGX), ABI 10: a true-color canvas for games and tools.
 * Not a desktop: the program owns the whole screen, draws into its own
 * 0x00RRGGBB pixel buffer and presents it. UEFI shows it on the GOP
 * framebuffer; Legacy BIOS uses Bochs/QEMU VBE or VGA 320x200x256, so the
 * same program runs with graphics on both firmwares. No heap, no FPU. */
typedef struct { unsigned int *pixels; int width, height; } BgxCanvas;

#define BGX_RGB(r, g, b) ((unsigned int)(r) << 16 | (unsigned int)(g) << 8 | (unsigned int)(b))
#define BGX_BLACK 0x000000U
#define BGX_WHITE 0xffffffU

/* Enter graphics. pixels holds width*height values; keep it static. */
int bgx_open(BgxCanvas *canvas, unsigned int *pixels, int width, int height);
int bgx_present(const BgxCanvas *canvas);
void bgx_close(void);   /* back to the text console (also done at exit) */
int bgx_display(int *width, int *height);

/* Drawing, clipped to the canvas. alpha: 0 transparent .. 255 opaque. */
void bgx_clear(BgxCanvas *c, unsigned int color);
void bgx_fill(BgxCanvas *c, int x, int y, int w, int h, unsigned int color);
void bgx_blend(BgxCanvas *c, int x, int y, int w, int h, unsigned int color, int alpha);
void bgx_frame(BgxCanvas *c, int x, int y, int w, int h, int thickness, unsigned int color);
/* src is 0xAARRGGBB (alpha blended), enlarged by an integer scale >= 1. */
void bgx_image(BgxCanvas *c, int x, int y, const unsigned int *src, int w, int h, int scale);
/* Pixel-doubling copy of a smaller canvas (e.g. a 3D view) to x, y. */
void bgx_upscale(BgxCanvas *c, int x, int y, const BgxCanvas *src, int scale);
/* UTF-8 text in the 8x16 system font; returns the advance in pixels. */
int bgx_text(BgxCanvas *c, int x, int y, const char *text, unsigned int color, int scale);
int bgx_text_shadow(BgxCanvas *c, int x, int y, const char *text, unsigned int color, int scale);
int bgx_text_width(const char *text, int scale);
void bgx_cursor(BgxCanvas *c, int x, int y);

/* Input. Call bgx_poll until it returns 0 once per frame: it updates key
 * state, mouse position, buttons, wheel and relative motion. Key codes are
 * set-1 make codes, BGX_KEY_*; extended keys have bit 7 set. */
#define BGX_PRESS 1
#define BGX_RELEASE 2
typedef struct { int type; unsigned int code, key; int repeat; } BgxEvent;
int bgx_poll(BgxEvent *event);
int bgx_key_held(unsigned int code);
void bgx_keys_release(void);   /* forget held keys (after a nested run) */
typedef struct {
    int x, y, dx, dy, wheel, available;    /* dx/dy/wheel: since last frame */
    unsigned int buttons, pressed, released;
} BgxMouse;
/* Mouse state accumulated since the previous bgx_mouse call. */
void bgx_mouse(BgxMouse *mouse);

/* Monotonic milliseconds from the time stamp counter. */
unsigned int bgx_millis(void);

/* All processor cores (ABI 11): job(argument, i) runs once for every i in
 * 0..count-1, spread over the cores, and bgx_parallel returns when all are
 * done. Jobs only compute: no bgx_ or barnix-> calls inside. Older kernels
 * run the jobs one after another. Split work into more jobs than cores
 * (e.g. bands of 8 rows) so fast cores take more. */
unsigned int bgx_cpus(void);
void bgx_parallel(void (*job)(void *argument, unsigned int index), void *argument, unsigned int count);

#define BGX_KEY_ESC 0x01
#define BGX_KEY_1 0x02
#define BGX_KEY_9 0x0a
#define BGX_KEY_0 0x0b
#define BGX_KEY_BACKSPACE 0x0e
#define BGX_KEY_TAB 0x0f
#define BGX_KEY_Q 0x10
#define BGX_KEY_W 0x11
#define BGX_KEY_E 0x12
#define BGX_KEY_R 0x13
#define BGX_KEY_ENTER 0x1c
#define BGX_KEY_CTRL 0x1d
#define BGX_KEY_A 0x1e
#define BGX_KEY_S 0x1f
#define BGX_KEY_D 0x20
#define BGX_KEY_F 0x21
#define BGX_KEY_SHIFT 0x2a
#define BGX_KEY_Z 0x2c
#define BGX_KEY_X 0x2d
#define BGX_KEY_C 0x2e
#define BGX_KEY_SPACE 0x39
#define BGX_KEY_F1 0x3b
#define BGX_KEY_F3 0x3d
#define BGX_KEY_UP 0xc8
#define BGX_KEY_LEFT 0xcb
#define BGX_KEY_RIGHT 0xcd
#define BGX_KEY_DOWN 0xd0
#ifdef __cplusplus
}
#endif
#endif
