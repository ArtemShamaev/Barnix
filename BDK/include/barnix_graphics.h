#ifndef BARNIX_GRAPHICS_H
#define BARNIX_GRAPHICS_H
#include <barnix_api.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix Graphic Library: EGA palette, logical 640x400 pixels, no heap. */
#define BGL_WIDTH 640
#define BGL_HEIGHT 400
#define BGL_MAX_WIDGETS 32
#define BGL_EVENT_NONE 0
#define BGL_EVENT_CLICK 1
#define BGL_EVENT_CLOSE 2
#define BGL_EVENT_SELECT 3
#define BGL_BUTTON 1
#define BGL_LIST 2
#define BGL_LABEL 3

typedef struct { int type,id,value; unsigned int key; } BglEvent;
typedef struct {
    int type,id,x,y,w,h,selected,first,count;
    const char *text;
    const char *const *items;
} BglWidget;
typedef struct {
    int x,y,w,h,drag,drag_x,drag_y,focus,capture,count,pixel_mode;
    const char *title;
    const char *desktop_title, *hint;
    BarnixPointer previous;
    BglWidget widgets[BGL_MAX_WIDGETS];
    unsigned char pixels[BGL_WIDTH*BGL_HEIGHT];
    unsigned short cells[80*25];
} BglApp;
/* Keep BglApp static: its canvas is larger than the application's stack. */
void bgl_init(BglApp *app,const char *title,int x,int y,int width,int height);
int bgl_button(BglApp *app,int id,int x,int y,int width,const char *text);
int bgl_list(BglApp *app,int id,int x,int y,int width,int height,const char *const *items,int count);
BglEvent bgl_poll(BglApp *app);
void bgl_draw(BglApp *app);
int bgl_label(BglApp *app,int id,int x,int y,int width,const char *text);
void bgl_close(BglApp *app);
void bgl_fill(BglApp *app,int x,int y,int width,int height,int color);
void bgl_text(BglApp *app,int x,int y,const char *text,int foreground,int background);
#ifdef __cplusplus
}
#endif
#endif
