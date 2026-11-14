#ifndef CONSOLE_VIDEO_H
#define CONSOLE_VIDEO_H
#include "multiboot.h"
extern unsigned short *console_cells;
void console_video_init(const MultibootInfo *info);
int console_graphics(void);
int console_graphic_blit(const unsigned char *pixels);
int console_display(unsigned int *width, unsigned int *height);
int console_rgb_blit(const unsigned int *pixels, unsigned int width, unsigned int height);
void console_present(void);
/* Window mode: separate cells, drawn by draw() on every change; 0 ends it. */
void console_window(void (*draw)(const unsigned short *cells, int cursor));
int console_windowed(void);
void console_blit(const unsigned short *cells);
void console_video_cursor(int x, int y);
void console_cursor_visible(int visible);
unsigned char console_glyph_row(unsigned int code, unsigned int y);
#endif
