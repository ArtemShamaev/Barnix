/* ABI 10 true-color output. A GOP framebuffer (UEFI) is drawn by
 * console_video.c. A Legacy BIOS text console has no framebuffer, so the
 * frame goes to the Bochs VBE extension (QEMU, Bochs, VirtualBox) or, on
 * other VGA hardware, to mode 13h: 320x200 with a 6x8x5 color cube and 4x4
 * ordered dithering. Nothing here calls the BIOS: registers, DAC, font and
 * text cells are saved on entry and written back when the program leaves. */
#include "video_rgb.h"
#include "audio.h"
#include "console_video.h"
#include "linux_memory.h"
#include "barnix.h"

#define TEXT_MODE 0
#define BOCHS_MODE 1
#define VGA_MODE 2
/* The Bochs mode follows the first frame a program presents: from 640x480
 * up to 1920x1200 when the video memory holds it, 800x600 otherwise (and
 * for small frames, which are scaled up). */
#define BOCHS_DEFAULT_WIDTH 800
#define BOCHS_DEFAULT_HEIGHT 600
#define BOCHS_MAX_WIDTH 1920
#define BOCHS_MAX_HEIGHT 1200
static unsigned int bochs_width = BOCHS_DEFAULT_WIDTH, bochs_height = BOCHS_DEFAULT_HEIGHT, bochs_memory;
/* Draw below the first 256 KiB of video memory, which holds the VGA planes
 * (text cells and font), so the text console survives untouched. */
static unsigned int bochs_skip_rows = 96;
#define BOCHS_WIDTH bochs_width
#define BOCHS_HEIGHT bochs_height
#define BOCHS_SKIP_ROWS bochs_skip_rows

static int legacy_vga, mode;
static unsigned int bochs_lfb;
typedef struct {
    unsigned char misc, seq[5], crtc[25], gc[9], ac[21], pel, dac[768];
} VgaState;
static VgaState saved;
static unsigned char saved_font[8192], saved_text[4000];
static unsigned char cube_r[16][256], cube_g[16][256], cube_b[16][256];
static unsigned int row[BOCHS_MAX_WIDTH], vga_shadow[320 * 200 / 4];
static const unsigned char bayer[4][4] = {{0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5}};

static inline void outb(unsigned short port, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static inline unsigned char inb(unsigned short port) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outw(unsigned short port, unsigned short v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(port)); }
static inline unsigned short inw(unsigned short port) { unsigned short v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outl(unsigned short port, unsigned int v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(port)); }
static inline unsigned int inl(unsigned short port) { unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port)); return v; }

static unsigned short bochs_read(unsigned short index) { outw(0x1ce, index); return inw(0x1cf); }
static void bochs_write(unsigned short index, unsigned short value) { outw(0x1ce, index); outw(0x1cf, value); }
static unsigned int pci_read(unsigned int bus, unsigned int slot, unsigned int fn, unsigned int reg)
{
    outl(0xcf8, 0x80000000U | bus << 16 | slot << 11 | fn << 8 | (reg & ~3U));
    return inl(0xcfc);
}
/* Linear framebuffer: BAR0 of the QEMU/Bochs (1234:1111) or VirtualBox
 * (80ee:beef) display adapter. */
static unsigned int find_bochs(void)
{
    unsigned short id = bochs_read(0);
    if (id < 0xb0c2 || id > 0xb0cf) return 0;
    for (unsigned int bus = 0; bus < 8; bus++)
        for (unsigned int slot = 0; slot < 32; slot++) {
            unsigned int vendor = pci_read(bus, slot, 0, 0);
            if ((vendor & 0xffff) == 0xffff) continue;
            if (vendor != 0x11111234U && vendor != 0xbeef80eeU) continue;
            if (pci_read(bus, slot, 0, 8) >> 24 != 3) continue;
            unsigned int bar = pci_read(bus, slot, 0, 0x10);
            if (bar & 1 || (bar & ~15U) < 0x10000000U) continue;
            return bar & ~15U;
        }
    return 0;
}

void video_legacy_init(void)
{
    legacy_vga = 1;
    bochs_lfb = find_bochs();
    bochs_memory = bochs_lfb ? (unsigned int)bochs_read(0xa) << 16 : 0;   /* VBE_DISPI_INDEX_VIDEO_MEMORY_64K */
    if (bochs_memory < (4u << 20)) bochs_memory = 4u << 20;
    if (bochs_memory > (16u << 20)) bochs_memory = 16u << 20;
    if (bochs_lfb && linux_memory_map_mmio(bochs_lfb, bochs_memory)) bochs_lfb = 0;
    /* Mode 13h ordered-dither tables: per channel and threshold, the
     * already-weighted component of the 6x8x5 cube index. */
    for (unsigned int t = 0; t < 16; t++)
        for (unsigned int v = 0; v < 256; v++) {
            cube_r[t][v] = (v * 5 * 16 + t * 255 + 127) / 4080 * 40;
            cube_g[t][v] = (v * 7 * 16 + t * 255 + 127) / 4080 * 5;
            cube_b[t][v] = (v * 4 * 16 + t * 255 + 127) / 4080;
        }
}

static void vga_save(VgaState *s)
{
    s->misc = inb(0x3cc);
    for (int i = 0; i < 5; i++) { outb(0x3c4, i); s->seq[i] = inb(0x3c5); }
    for (int i = 0; i < 25; i++) { outb(0x3d4, i); s->crtc[i] = inb(0x3d5); }
    for (int i = 0; i < 9; i++) { outb(0x3ce, i); s->gc[i] = inb(0x3cf); }
    for (int i = 0; i < 21; i++) { inb(0x3da); outb(0x3c0, i); s->ac[i] = inb(0x3c1); }
    inb(0x3da); outb(0x3c0, 0x20);
    s->pel = inb(0x3c6);
    outb(0x3c7, 0);
    for (int i = 0; i < 768; i++) s->dac[i] = inb(0x3c9);
}
static void vga_load(const VgaState *s, int with_dac)
{
    outb(0x3c4, 0); outb(0x3c5, 1);           /* synchronous reset while the clock changes */
    outb(0x3c2, s->misc);
    for (int i = 1; i < 5; i++) { outb(0x3c4, i); outb(0x3c5, s->seq[i]); }
    outb(0x3c4, 0); outb(0x3c5, s->seq[0]);
    outb(0x3d4, 0x11); outb(0x3d5, inb(0x3d5) & 0x7f);   /* unlock CRTC 0-7 */
    for (int i = 0; i < 25; i++) { outb(0x3d4, i); outb(0x3d5, i == 0x11 ? s->crtc[i] & 0x7f : s->crtc[i]); }
    for (int i = 0; i < 9; i++) { outb(0x3ce, i); outb(0x3cf, s->gc[i]); }
    for (int i = 0; i < 21; i++) { inb(0x3da); outb(0x3c0, i); outb(0x3c0, s->ac[i]); }
    inb(0x3da); outb(0x3c0, 0x20);
    outb(0x3d4, 0x11); outb(0x3d5, s->crtc[0x11]);
    if (!with_dac) return;
    outb(0x3c6, s->pel);
    outb(0x3c8, 0);
    for (int i = 0; i < 768; i++) outb(0x3c9, s->dac[i]);
}
/* Plane 2 holds the character generator. Read or write it linearly. */
static void font_plane(int write)
{
    outb(0x3c4, 1); outb(0x3c5, saved.seq[1] | 0x20);
    outb(0x3c4, 2); outb(0x3c5, 4);
    outb(0x3c4, 4); outb(0x3c5, 7);
    outb(0x3ce, 1); outb(0x3cf, 0);
    outb(0x3ce, 3); outb(0x3cf, 0);
    outb(0x3ce, 4); outb(0x3cf, 2);
    outb(0x3ce, 5); outb(0x3cf, 0);
    outb(0x3ce, 6); outb(0x3cf, 4);
    outb(0x3ce, 8); outb(0x3cf, 255);
    volatile unsigned char *plane = (volatile unsigned char *)0xa0000;
    for (unsigned int i = 0; i < sizeof(saved_font); i++) {
        if (write) plane[i] = saved_font[i]; else saved_font[i] = plane[i];
    }
}
static void save_text_mode(void)
{
    volatile unsigned char *text = (volatile unsigned char *)0xb8000;
    for (unsigned int i = 0; i < sizeof(saved_text); i++) saved_text[i] = text[i];
    vga_save(&saved);
    font_plane(0);
    vga_load(&saved, 0);
}

static void enter_vga(void)
{
    static const VgaState mode13 = {
        0x63, {0x03, 0x01, 0x0f, 0x00, 0x0e},
        {0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f, 0x00, 0x41, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x9c, 0x0e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff},
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
         0x0c, 0x0d, 0x0e, 0x0f, 0x41, 0x00, 0x0f, 0x00, 0x00},
        0xff, {0}};
    vga_load(&mode13, 0);
    outb(0x3c6, 0xff);
    outb(0x3c8, 0);
    for (int i = 0; i < 240; i++) {
        outb(0x3c9, i / 40 * 63 / 5); outb(0x3c9, i / 5 % 8 * 63 / 7); outb(0x3c9, i % 5 * 63 / 4);
    }
    for (int i = 0; i < 16; i++) { unsigned char g = i * 63 / 15; outb(0x3c9, g); outb(0x3c9, g); outb(0x3c9, g); }
    volatile unsigned int *screen = (volatile unsigned int *)0xa0000;
    for (int i = 0; i < 320 * 200 / 4; i++) screen[i] = vga_shadow[i] = 0;
}
static void enter_bochs(unsigned int w, unsigned int h)
{
    w &= ~7u;
    unsigned int skip = (262144 + w * 4 - 1) / (w ? w * 4 : 1);
    if (w >= 640 && h >= 480 && w <= BOCHS_MAX_WIDTH && h <= BOCHS_MAX_HEIGHT && (unsigned long long)w * 4 * (h + skip) <= bochs_memory) {
        bochs_width = w; bochs_height = h; bochs_skip_rows = skip;
    } else {
        bochs_width = BOCHS_DEFAULT_WIDTH; bochs_height = BOCHS_DEFAULT_HEIGHT; bochs_skip_rows = 96;
    }
    bochs_write(4, 0);
    bochs_write(1, BOCHS_WIDTH); bochs_write(2, BOCHS_HEIGHT); bochs_write(3, 32);
    bochs_write(4, 0x01 | 0x40 | 0x80);     /* enabled, linear framebuffer, keep memory */
    bochs_write(6, BOCHS_WIDTH);
    bochs_write(9, BOCHS_SKIP_ROWS);
    if (bochs_read(1) != BOCHS_WIDTH || bochs_read(2) != BOCHS_HEIGHT || bochs_read(3) != 32 ||
        bochs_read(9) != BOCHS_SKIP_ROWS) {
        bochs_write(4, 0); vga_load(&saved, 1); bochs_lfb = 0; enter_vga(); mode = VGA_MODE; return;
    }
    volatile unsigned int *screen = (volatile unsigned int *)(uintptr_t)(bochs_lfb + BOCHS_SKIP_ROWS * BOCHS_WIDTH * 4);
    for (unsigned int i = 0; i < BOCHS_WIDTH * BOCHS_HEIGHT; i++) screen[i] = 0;
}

void video_leave(void)
{
    if (mode == TEXT_MODE) return;
    if (mode == BOCHS_MODE) bochs_write(4, 0);
    vga_load(&saved, 1);
    font_plane(1);
    vga_load(&saved, 1);
    volatile unsigned char *text = (volatile unsigned char *)0xb8000;
    for (unsigned int i = 0; i < sizeof(saved_text); i++) text[i] = saved_text[i];
    mode = TEXT_MODE;
}

int video_display(unsigned int *width, unsigned int *height)
{
    if (!console_display(width, height)) return 0;
    if (!legacy_vga) return -1;
    if (width) *width = bochs_lfb ? (mode == BOCHS_MODE ? BOCHS_WIDTH : BOCHS_DEFAULT_WIDTH) : 320;
    if (height) *height = bochs_lfb ? (mode == BOCHS_MODE ? BOCHS_HEIGHT : BOCHS_DEFAULT_HEIGHT) : 200;
    return 0;
}

int video_present(const unsigned int *pixels, unsigned int w, unsigned int h)
{
    audio_update();
    if (!pixels) { console_graphic_blit(0); video_leave(); return 0; }
    if (console_graphics()) return console_rgb_blit(pixels, w, h);
    if (!legacy_vga || !w || !h || w > 4096 || h > 4096) return -1;
    if (mode == TEXT_MODE) {
        save_text_mode();
        mode = bochs_lfb ? BOCHS_MODE : VGA_MODE;
        if (mode == BOCHS_MODE) enter_bochs(w, h); else enter_vga();
    }
    unsigned int dw = mode == BOCHS_MODE ? BOCHS_WIDTH : 320, dh = mode == BOCHS_MODE ? BOCHS_HEIGHT : 200;
    unsigned int up = 1, down = 1;
    if (w <= dw && h <= dh) { up = dw / w; if (dh / h < up) up = dh / h; }
    else { down = (w + dw - 1) / dw; if ((h + dh - 1) / dh > down) down = (h + dh - 1) / dh; }
    unsigned int ow = w * up / down, oh = h * up / down, ox = (dw - ow) / 2, oy = (dh - oh) / 2;
    for (unsigned int y = 0; y < h / down; y++) {
        const unsigned int *src = pixels + y * down * w;
        for (unsigned int x = 0; x < w / down; x++) {
            unsigned int c = src[x * down];
            if (down > 1) {
                unsigned int r = 0, g = 0, b = 0, n = down * down;
                for (unsigned int j = 0; j < down; j++)
                    for (unsigned int i = 0; i < down; i++) {
                        unsigned int v = src[j * w + x * down + i];
                        r += v >> 16 & 255; g += v >> 8 & 255; b += v & 255;
                    }
                c = (r / n) << 16 | (g / n) << 8 | b / n;
            }
            for (unsigned int i = 0; i < up; i++) row[x * up + i] = c;
        }
        for (unsigned int i = 0; i < up; i++) {
            unsigned int line = oy + y * up + i;
            if (mode == BOCHS_MODE) {
                volatile unsigned int *out = (volatile unsigned int *)(uintptr_t)(bochs_lfb +
                    ((BOCHS_SKIP_ROWS + line) * BOCHS_WIDTH + ox) * 4);
                for (unsigned int x = 0; x < ow; x++) out[x] = row[x];
            } else {
                /* Write only changed dwords: VGA memory is slow (and trapped
                 * by virtual machines), while most of a frame often stays. */
                unsigned char indexes[320 + 8];
                const unsigned char *t = bayer[line & 3];
                unsigned int first = (line * 320 + ox) & ~3U, last = (line * 320 + ox + ow + 3) & ~3U;
                unsigned char *packed = indexes + ((line * 320 + ox) & 3);
                for (unsigned int x = 0; x < ow; x++) {
                    unsigned int c = row[x], k = t[(ox + x) & 3];
                    packed[x] = cube_r[k][c >> 16 & 255] + cube_g[k][c >> 8 & 255] + cube_b[k][c & 255];
                }
                for (unsigned int a = first; a < last; a += 4) {
                    unsigned int value = vga_shadow[a / 4];
                    for (unsigned int b = 0; b < 4; b++) {
                        unsigned int position = a + b - (line * 320 + ox);
                        if (position < ow) value = (value & ~(255U << b * 8)) | (unsigned int)packed[position] << b * 8;
                    }
                    if (value != vga_shadow[a / 4]) { vga_shadow[a / 4] = value; ((volatile unsigned int *)0xa0000)[a / 4] = value; }
                }
            }
        }
    }
    return 0;
}
