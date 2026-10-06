#define BARNIX_APP_NAME "app.elf"
#include <barnix_graphics>
#include <barnix_api>
using namespace Barnix;
// Canvas storage must be static, not on the 64 KiB application stack.
static GraphicApplication app("Barnix Executive");
static const char *const items[]={"Welcome to Barnix", "DOS-style windows", "USB mouse and wheel", "Keyboard navigation", "Native ELF application"};
int main(){
    app.List(1,16,40,480,176,items,5);
    app.Button(2,16,240,152,"Open");
    app.Button(3,336,240,160,"Exit");
    for(;;){
        auto event=app.Poll();
        if(event.type==BGL_EVENT_CLOSE||(event.type==BGL_EVENT_CLICK&&event.id==3))break;
        app.Draw();Thread::Sleep(10);
    }
    app.Close();return 0;
}
