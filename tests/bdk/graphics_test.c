#include <barnix_graphics.h>
#include <assert.h>
#include <limits.h>
#include "text.h"
static BglApp app;
static BarnixPointer state;
static unsigned int key;
static int fallback, frames, screens, cleared, closed;
static unsigned int poll(void) { unsigned int result=key; key=0; return result; }
static void pointer(BarnixPointer *p) { *p=state; }
static unsigned char glyph(unsigned int c,unsigned int row) { (void)c;(void)row;return 255; }
static int graphics(const unsigned char *pixels) { if(!pixels){closed++;return 0;}frames++;return fallback?-1:0; }
static void screen(const unsigned short *cells) { assert(cells==app.cells);screens++; }
static void clear(void) { cleared++; }
static BarnixAPI api={.poll_key=poll,.pointer=pointer,.glyph=glyph,.graphics=graphics,.screen=screen,.clear=clear};
const BarnixAPI *barnix=&api;
int main(void) {
    static const char *const items[]={"one","two","three","four"};
    bgl_init(&app,"Привет",64,48,512,304);
    assert(bgl_button(&app,1,16,240,160,"Привет") == 0);
    assert(bgl_button(&app,1,16,240,160,"duplicate") == -1);
    assert(bgl_list(&app,2,16,40,480,32,items,4) == 0);
    assert(bgl_list(&app,3,16,40,480,32,0,1) == -1);
    bgl_draw(&app); assert(frames==1 && screens==0 && app.pixel_mode);
    assert((app.cells[(52/16)*80+72/8]&255)==text_glyph(0x41f));
    assert((app.cells[(52/16)*80+80/8]&255)==text_glyph(0x440));
    key='\n';BglEvent e=bgl_poll(&app);assert(e.type==BGL_EVENT_CLICK&&e.id==1);
    key='\t';bgl_poll(&app);key=KEY_DOWN;e=bgl_poll(&app);
    assert(e.type==BGL_EVENT_SELECT&&e.id==2&&e.value==1);
    key=KEY_DOWN;bgl_poll(&app);assert(app.widgets[1].first==1);
    state.mouse=(BarnixMouse){84,292,1,0,1};bgl_poll(&app);
    state.mouse.buttons=0;e=bgl_poll(&app);assert(e.type==BGL_EVENT_CLICK&&e.id==1);
    state.mouse=(BarnixMouse){84,58,1,0,1};bgl_poll(&app);
    state.mouse.x=104;state.mouse.y=78;bgl_poll(&app);assert(app.x==84&&app.y==68);
    state.mouse.buttons=0;bgl_poll(&app);
    fallback=1;state.mouse.x=-100;state.mouse.y=-100;bgl_poll(&app);bgl_draw(&app);
    assert(screens==1&&!app.pixel_mode);
    bgl_fill(&app,INT_MAX,INT_MIN,INT_MAX,INT_MAX,15);
    bgl_fill(&app,0,0,640,400,0);
    bgl_fill(&app,-10000,0,10005,1,7);
    assert(app.pixels[4]==7&&app.pixels[5]==0);
    bgl_text(&app,0,0,"\xd0" "A",15,0); // Invalid continuation must retain A.
    assert((app.cells[0]&255)=='?'&&(app.cells[1]&255)=='A');
    key=KEY_ESCAPE;assert(bgl_poll(&app).type==BGL_EVENT_CLOSE);
    assert(bgl_label(&app,4,16,24,200,"Status") == 0);
    app.focus=1;key='\t';bgl_poll(&app);assert(app.focus==0);
    key='\t';bgl_poll(&app);key=KEY_END;e=bgl_poll(&app);
    assert(e.type==BGL_EVENT_SELECT&&e.value==3);
    key=KEY_HOME;e=bgl_poll(&app);assert(e.value==0);
    bgl_close(&app);assert(closed==1&&cleared==1);
    return 0;
}
