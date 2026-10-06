#define BARNIX_APP_NAME "app.elf"
#include <barnix_graphics.h>
static BglApp app;
static const char *const items[]={"Welcome to Barnix", "DOS-style windows", "USB mouse and wheel", "Keyboard navigation", "Native ELF application"};
int main(void){
    bgl_init(&app,"Barnix Executive",64,48,512,304);
    bgl_list(&app,1,16,40,480,176,items,5);
    bgl_button(&app,2,16,240,152,"About");
    bgl_button(&app,3,336,240,160,"Exit");
    for(;;){
        BglEvent event=bgl_poll(&app);
        if(event.type==BGL_EVENT_CLOSE||(event.type==BGL_EVENT_CLICK&&event.id==3))break;
        if(event.type==BGL_EVENT_CLICK&&event.id==2)app.title="Barnix Graphic Library - BGL";
        bgl_draw(&app);barnix_sleep(10);
    }
    bgl_close(&app);return 0;
}
