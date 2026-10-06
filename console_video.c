#include "console_video.h"
#include "linux_memory.h"
#include "barnix.h"

unsigned short *console_cells = (unsigned short *)0xb8000;
unsigned short console_text[80 * 25];
static unsigned short previous[80 * 25];
static unsigned int framebuffer, pitch, width, height, bytes_per_pixel;
static unsigned int colors[16], origin_x, origin_y;
static int graphic_active;
static unsigned char graphic_previous[640*400];
static int cursor = -1, visible = 1, previous_cursor = -1;
int console_graphics(void) { return framebuffer != 0; }

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
    framebuffer = info->framebuffer_low; pitch = stride;
    width = info->framebuffer_width; height = info->framebuffer_height; bytes_per_pixel = bpp;
    origin_x = (width - 640) / 2; origin_y = (height - 400) / 2;
    static const unsigned char palette[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    for (int i = 0; i < 16; i++) colors[i] = (palette[i][0] << info->red_position) |
        (palette[i][1] << info->green_position) | (palette[i][2] << info->blue_position);
    for (unsigned int y = 0; y < height; y++)
        memset((void *)(framebuffer + y * pitch), 0, width * bpp);
    console_cells = console_text;
    for (int i = 0; i < 2000; i++) { console_text[i] = 0x0720; previous[i] = 0xffff; }
}
void console_present(void)
{
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
            unsigned char *line = (unsigned char *)(framebuffer + (origin_y + i / 80 * 16 + y) * pitch +
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
    visible=value;
    if(!framebuffer) {
        unsigned char mode;
        __asm__ volatile("outb %0,%1"::"a"((unsigned char)0x0a),"Nd"((unsigned short)0x3d4));
        __asm__ volatile("inb %1,%0":"=a"(mode):"Nd"((unsigned short)0x3d5));
        mode=value?(mode&~0x20):(mode|0x20);
        __asm__ volatile("outb %0,%1"::"a"(mode),"Nd"((unsigned short)0x3d5));
    }
    console_present();
}

void console_blit(const unsigned short *cells) {
    if(!cells)return;
    console_cursor_visible(0);
    for(int i=0;i<2000;i++)console_cells[i]=cells[i];
    console_present();
}

int console_graphic_blit(const unsigned char *pixels) {
    if(!pixels){
        graphic_active=0;
        for(int i=0;i<2000;i++)previous[i]=0xffff;
        console_present();return 0;
    }
    if(!framebuffer)return -1;
    for(unsigned int y=0;y<400;y++)for(unsigned int x=0;x<640;x++){
        unsigned int index=y*640+x,color=pixels[index]&15;
        if(graphic_active && graphic_previous[index]==color)continue;
        graphic_previous[index]=color;
        unsigned char *out=(unsigned char *)(framebuffer+(origin_y+y)*pitch+(origin_x+x)*bytes_per_pixel);
        for(unsigned int b=0;b<bytes_per_pixel;b++)out[b]=colors[color]>>(b*8);
    }
    graphic_active=1;return 0;
}
