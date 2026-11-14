#include "console_video.h"
#include "linux_memory.h"
#include "barnix.h"
#include "memory.h"

unsigned short *console_cells = (unsigned short *)0xb8000;
unsigned short console_text[80 * 25];
static unsigned short previous[80 * 25];
static unsigned int framebuffer, pitch, width, height, bytes_per_pixel;
static unsigned int colors[16], origin_x, origin_y;
static int graphic_active, rgb_active, shadow_valid;
static unsigned int *rgb_shadow, shadow_capacity, shadow_width, shadow_height;
static unsigned int red_shift, green_shift, blue_shift;
static unsigned char graphic_previous[640*400];
static int cursor = -1, visible = 1, previous_cursor = -1;
static int visibility_initialized;
int console_graphics(void) { return framebuffer != 0; }

static void cpuid(unsigned int leaf, unsigned int *a, unsigned int *d)
{ unsigned int b, c; __asm__ volatile("cpuid" : "=a"(*a), "=b"(b), "=c"(c), "=d"(*d) : "a"(leaf), "c"(0)); }
static void rdmsr(unsigned int msr, unsigned int *lo, unsigned int *hi)
{ __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr)); }
static void wrmsr(unsigned int msr, unsigned int lo, unsigned int hi)
{ __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(msr) : "memory"); }
/* Native programs run without paging, so the framebuffer's memory type comes
 * from the MTRRs, usually uncached: every store is a bus transaction. Cover
 * it with a free variable MTRR of type write-combining when that is safe. */
static void framebuffer_write_combining(unsigned int base, unsigned int bytes)
{
    unsigned int a, d, lo, hi, size = 4096;
    cpuid(0, &a, &d);
    if (a < 1) return;
    cpuid(1, &a, &d);
    if (!(d & (1U << 12))) return;                 /* no MTRRs */
    rdmsr(0xfe, &lo, &hi);
    unsigned int count = lo & 255;
    if (!(lo & (1U << 10)) || !count) return;     /* no write-combining */
    rdmsr(0x2ff, &lo, &hi);
    if (!(lo & (1U << 11))) return;               /* MTRRs disabled */
    while (size < bytes && size < 0x40000000U) size <<= 1;
    if (size < bytes || base & (size - 1)) return;
    unsigned int width = 36;
    cpuid(0x80000000U, &a, &d);
    if (a >= 0x80000008U) { cpuid(0x80000008U, &a, &d); width = a & 255; }
    if (width < 32 || width > 52) return;
    unsigned int mask = ~(size - 1), free_slot = count;
    for (unsigned int i = 0; i < count && i < 32; i++) {
        unsigned int base_lo, base_hi, mask_lo, mask_hi;
        rdmsr(0x200 + 2 * i, &base_lo, &base_hi);
        rdmsr(0x201 + 2 * i, &mask_lo, &mask_hi);
        if (!(mask_lo & (1U << 11))) { if (free_slot == count) free_slot = i; continue; }
        if (base_hi) continue;
        unsigned int other = mask_lo & ~4095U, start = base_lo & ~4095U;
        /* Overlaps with WB or UC ranges are undefined or make WC useless. */
        if ((base & other) == (start & other) || (start & mask) == (base & mask)) return;
    }
    if (free_slot == count) return;
    unsigned int cr0, def_lo, def_hi;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile("mov %0,%%cr0; wbinvd" :: "r"((cr0 | 0x40000000U) & ~0x20000000U) : "memory");
    rdmsr(0x2ff, &def_lo, &def_hi);
    wrmsr(0x2ff, def_lo & ~(1U << 11), def_hi);
    wrmsr(0x200 + 2 * free_slot, base | 1, 0);    /* type 1: write-combining */
    wrmsr(0x201 + 2 * free_slot, mask | (1U << 11), (1U << (width - 32)) - 1);
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(0x2ff, def_lo, def_hi);
    __asm__ volatile("mov %0,%%cr0" :: "r"(cr0) : "memory");
}

void console_video_init(const MultibootInfo *info)
{
    if (!info || !(info->flags & (1U << 12)) || info->framebuffer_type != 1 ||
        info->framebuffer_high || (info->framebuffer_bpp != 24 && info->framebuffer_bpp != 32) ||
        info->framebuffer_width < 640 || info->framebuffer_height < 400 ||
        info->framebuffer_width > 8192 || info->framebuffer_height > 8192 ||
        info->red_size != 8 || info->green_size != 8 || info->blue_size != 8 ||
        info->red_position > info->framebuffer_bpp - 8 ||
        info->green_position > info->framebuffer_bpp - 8 || info->blue_position > info->framebuffer_bpp - 8) return;
    unsigned int bpp = info->framebuffer_bpp / 8, stride = info->framebuffer_pitch;
    if (stride < info->framebuffer_width * bpp || stride > 65536 ||
        linux_memory_map_mmio(info->framebuffer_low, stride * info->framebuffer_height)) return;
    framebuffer_write_combining(info->framebuffer_low, stride * info->framebuffer_height);
    framebuffer = info->framebuffer_low; pitch = stride;
    /* Last presented frame, so unchanged pixels never touch video memory. */
    rgb_shadow = memory_kernel_alloc(info->framebuffer_width * info->framebuffer_height * 4);
    if (rgb_shadow) shadow_capacity = info->framebuffer_width * info->framebuffer_height;
    width = info->framebuffer_width; height = info->framebuffer_height; bytes_per_pixel = bpp;
    origin_x = (width - 640) / 2; origin_y = (height - 400) / 2;
    red_shift = info->red_position; green_shift = info->green_position; blue_shift = info->blue_position;
    static const unsigned char palette[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    for (int i = 0; i < 16; i++) colors[i] = (palette[i][0] << info->red_position) |
        (palette[i][1] << info->green_position) | (palette[i][2] << info->blue_position);
    for (unsigned int y = 0; y < height; y++)
        memset((void *)(uintptr_t)(framebuffer + y * pitch), 0, width * bpp);
    console_cells = console_text;
    for (int i = 0; i < 2000; i++) { console_text[i] = 0x0720; previous[i] = 0xffff; }
}
/* Window mode (ABI 14 run_area): a program run from a Gouse window gets its
 * own cells, which a callback draws into that window instead of the screen. */
static void (*window_draw)(const unsigned short *cells, int cursor);
static unsigned short window_cells[80 * 25], *window_saved;
void console_window(void (*draw)(const unsigned short *cells, int cursor))
{
    if (draw && !window_draw) {
        window_saved = console_cells;
        for (int i = 0; i < 2000; i++) window_cells[i] = 0x0720;
        console_cells = window_cells;
    } else if (!draw && window_draw) {
        console_cells = window_saved;
        for (int i = 0; i < 2000; i++) previous[i] = 0xffff;
    }
    window_draw = draw;
}
int console_windowed(void) { return window_draw != 0; }
void console_present(void)
{
    if (window_draw) { window_draw(console_cells, visible ? cursor : -1); return; }
    if (!framebuffer || graphic_active) return;
    int active = visible ? cursor : -1;
    for (unsigned int i = 0; i < 2000; i++) {
        unsigned int cell = console_cells[i];
        if (cell == previous[i] && (int)i != active && (int)i != previous_cursor) continue;
        previous[i] = cell;
        unsigned int fg = colors[(cell >> 8) & 15], bg = colors[(cell >> 12) & 15];
        for (unsigned int y = 0; y < 16; y++) {
            unsigned int bits = console_glyph_row(cell & 255, y);
            if ((int)i == active && y >= 14) bits = 255;
            unsigned char *line = (unsigned char *)(uintptr_t)(framebuffer + (origin_y + i / 80 * 16 + y) * pitch +
                                                   (origin_x + i % 80 * 8) * bytes_per_pixel);
            for (unsigned int x = 0; x < 8; x++) {
                unsigned int color = bits & (128U >> x) ? fg : bg;
                for (unsigned int byte = 0; byte < bytes_per_pixel; byte++) line[x * bytes_per_pixel + byte] = color >> (byte * 8);
            }
        }
    }
    previous_cursor = active;
}
void console_video_cursor(int x, int y)
{ cursor = x >= 0 && x < 80 && y >= 0 && y < 25 ? y * 80 + x : -1; console_present(); }
void console_cursor_visible(int value) {
    value = value != 0;
    if (visibility_initialized && visible == value) return;
    visibility_initialized = 1;
    visible=value;
    if(!framebuffer) {
        unsigned char mode;
        __asm__ volatile("outb %0,%1"::"a"((unsigned char)0x0a),"Nd"((unsigned short)0x3d4));
        __asm__ volatile("inb %1,%0":"=a"(mode):"Nd"((unsigned short)0x3d5));
        mode=value?(mode&~0x20):(mode|0x20);
        if(!window_draw)__asm__ volatile("outb %0,%1"::"a"(mode),"Nd"((unsigned short)0x3d5));
    }
    console_present();
}

void console_blit(const unsigned short *cells) {
    if(!cells)return;
    console_cursor_visible(0);
    if(window_draw){for(int i=0;i<2000;i++)console_cells[i]=cells[i];console_present();return;}
    for(int i=0;i<2000;i++)console_cells[i]=cells[i];
    console_present();
}

static void clear_framebuffer(void)
{
    for (unsigned int y = 0; y < height; y++) memset((void *)(uintptr_t)(framebuffer + y * pitch), 0, width * bytes_per_pixel);
}
int console_display(unsigned int *out_width, unsigned int *out_height)
{
    if (!framebuffer) return -1;
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return 0;
}
/* XRGB8888 frame, enlarged by the largest integer factor that fits (or
 * reduced by an integer box filter when it is larger) and centered. */
int console_rgb_blit(const unsigned int *pixels, unsigned int w, unsigned int h)
{
    static unsigned int row[8192];
    static unsigned char packed[8192 * 4];
    if (!framebuffer || !pixels || !w || !h || w > 4096 || h > 4096) return -1;
    if (!rgb_active) { clear_framebuffer(); rgb_active = 1; shadow_valid = 0; }
    graphic_active = 1;
    unsigned int up = 1, down = 1;
    if (w <= width && h <= height) { up = width / w; if (height / h < up) up = height / h; }
    else { down = (w + width - 1) / width; if ((h + height - 1) / height > down) down = (h + height - 1) / height; }
    unsigned int ow = w * up / down, oh = h * up / down, ox = (width - ow) / 2, oy = (height - oh) / 2;
    int native = bytes_per_pixel == 4 && red_shift == 16 && green_shift == 8 && blue_shift == 0;
    if (bytes_per_pixel == 4 && down == 1 && rgb_shadow && w * h <= shadow_capacity) {
        /* Only pixels that changed since the last frame reach video memory. */
        if (!shadow_valid || shadow_width != w || shadow_height != h) {
            shadow_valid = 0; shadow_width = w; shadow_height = h;
        }
        for (unsigned int y = 0; y < h; y++) {
            const unsigned int *src = pixels + y * w;
            unsigned int *seen = rgb_shadow + y * w;
            unsigned int address = framebuffer + (oy + y * up) * pitch + ox * 4;
            for (unsigned int x = 0; x < w; x++) {
                unsigned int c = src[x] & 0xffffff;
                if (shadow_valid && seen[x] == c) continue;
                seen[x] = c;
                if (!native) c = (c >> 16 & 255) << red_shift | (c >> 8 & 255) << green_shift | (c & 255) << blue_shift;
                for (unsigned int i = 0; i < up; i++) {
                    volatile unsigned int *out = (volatile unsigned int *)(uintptr_t)(address + i * pitch) + x * up;
                    for (unsigned int k = 0; k < up; k++) out[k] = c;
                }
            }
        }
        shadow_valid = 1;
        return 0;
    }
    shadow_valid = 0;
    for (unsigned int y = 0; y < h / down; y++) {
        const unsigned int *src = pixels + y * down * w;
        for (unsigned int x = 0; x < w / down; x++) {
            unsigned int c = src[x * down];
            if (down > 1) {   /* box filter */
                unsigned int r = 0, g = 0, b = 0;
                for (unsigned int j = 0; j < down; j++)
                    for (unsigned int i = 0; i < down; i++) {
                        unsigned int v = src[j * w + x * down + i];
                        r += v >> 16 & 255; g += v >> 8 & 255; b += v & 255;
                    }
                unsigned int n = down * down;
                c = (r / n) << 16 | (g / n) << 8 | b / n;
            }
            if (!native) c = (c >> 16 & 255) << red_shift | (c >> 8 & 255) << green_shift | (c & 255) << blue_shift;
            for (unsigned int i = 0; i < up; i++) row[x * up + i] = c;
        }
        const void *line = row;
        if (bytes_per_pixel != 4) {
            for (unsigned int x = 0; x < ow; x++)
                for (unsigned int b = 0; b < bytes_per_pixel; b++) packed[x * bytes_per_pixel + b] = row[x] >> (b * 8);
            line = packed;
        }
        for (unsigned int i = 0; i < up; i++) {
            unsigned int address = framebuffer + (oy + y * up + i) * pitch + ox * bytes_per_pixel;
            if (bytes_per_pixel == 4) {   /* dword stores: the framebuffer is uncached */
                volatile unsigned int *out = (volatile unsigned int *)(uintptr_t)address;
                for (unsigned int x = 0; x < ow; x++) out[x] = row[x];
            } else memcpy((void *)(uintptr_t)address, line, ow * bytes_per_pixel);
        }
    }
    return 0;
}
int console_graphic_blit(const unsigned char *pixels) {
    if(!pixels){
        if(rgb_active){clear_framebuffer();rgb_active=0;}
        graphic_active=0;
        for(int i=0;i<2000;i++)previous[i]=0xffff;
        console_present();return 0;
    }
    if(!framebuffer)return -1;
    if(rgb_active){clear_framebuffer();rgb_active=0;graphic_active=0;}
    for(unsigned int y=0;y<400;y++)for(unsigned int x=0;x<640;x++){
        unsigned int index=y*640+x,color=pixels[index]&15;
        if(graphic_active && graphic_previous[index]==color)continue;
        graphic_previous[index]=color;
        unsigned char *out=(unsigned char *)(uintptr_t)(framebuffer+(origin_y+y)*pitch+(origin_x+x)*bytes_per_pixel);
        for(unsigned int b=0;b<bytes_per_pixel;b++)out[b]=colors[color]>>(b*8);
    }
    graphic_active=1;return 0;
}
