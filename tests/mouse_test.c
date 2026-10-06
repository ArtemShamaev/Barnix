#include <assert.h>
#include <stdio.h>
#include "mouse.h"
static BarnixMouse state(void){BarnixMouse s;mouse_get(&s);return s;}
static void packet(unsigned int flags,unsigned int x,unsigned int y){mouse_feed(flags);mouse_feed(x);mouse_feed(y);}
int main(void){
    mouse_reset();assert(state().x==320&&state().y==200&&!state().buttons);
    assert(!mouse_feed(0));assert(!mouse_feed(8));assert(!mouse_feed(10));assert(mouse_feed(20));
    assert(state().x==330&&state().y==180);
    packet(0x38,246,236);assert(state().x==320&&state().y==200);
    packet(9,0,0);assert(state().buttons==1);packet(10,0,0);assert(state().buttons==2);packet(8,0,0);assert(!state().buttons);
    packet(0xc9,255,255);assert(state().x==320&&state().y==200&&state().buttons==1);
    for(int i=0;i<8;i++)packet(0x18,128,127);
    assert(state().x==0&&state().y==0);
    for(int i=0;i<8;i++)packet(0x28,127,128);
    assert(state().x==639&&state().y==399);
    unsigned int sequence=state().sequence;packet(8,0,0);assert(state().sequence==sequence);
    mouse_reset();
    unsigned char report[]={0xff,128,127,99};
    assert(!mouse_usb_report(0,3));
    assert(!mouse_usb_report(report,2));assert(!state().available);
    assert(mouse_usb_report(report,sizeof(report)));
    assert(state().x==192&&state().y==327&&state().buttons==7);
    report[0]=0;report[1]=127;report[2]=128;
    assert(mouse_usb_report(report,3));
    assert(state().x==319&&state().y==199&&!state().buttons);
    report[1]=report[2]=0;sequence=state().sequence;
    assert(!mouse_usb_report(report,3));assert(state().sequence==sequence);
    report[0]=BARNIX_MOUSE_MIDDLE;assert(mouse_usb_report(report,3));
    assert(state().buttons==BARNIX_MOUSE_MIDDLE);
    puts("USB boot reports: signed deltas, button masks, short reports and idle passed");
    puts("PS/2 packet framing, signed deltas, buttons, overflow and clipping passed");
}
