#include "mouse.h"
/* Shared PS/2 and USB pointer. Coordinates are logical 640x400 pixels. */
static BarnixMouse ps2_mouse_state={320,200,0,0,0};
static unsigned char source_present[96];
static unsigned int source_buttons[96];
static int wheel_total,horizontal_total;
static unsigned char packet[3];
static int packet_index;
void mouse_reset(void) {ps2_mouse_state=(BarnixMouse){320,200,0,0,0};packet_index=0;wheel_total=horizontal_total=0;for(int i=0;i<96;i++){source_present[i]=0;source_buttons[i]=0;}}
void mouse_get(BarnixMouse *out) {if(out)*out=ps2_mouse_state;}
int mouse_feed(unsigned char byte) {
    if(!packet_index && !(byte&8))return 0;
    packet[packet_index++]=byte;if(packet_index<3)return 0;packet_index=0;
    int dx=packet[1]-((packet[0]&16)?256:0),dy=packet[2]-((packet[0]&32)?256:0);
    if(packet[0]&0xc0)dx=dy=0;
    return mouse_relative(dx,-dy,packet[0]&7);
}
/* HID boot protocol has no report ID; ignore optional trailing bytes. */
int mouse_usb_report(const unsigned char *report,unsigned int length) {
    if(!report || length<3)return 0;
    int dx=report[1]-(report[1]&128?256:0);
    int dy=report[2]-(report[2]&128?256:0);
    return mouse_relative(dx,dy,report[0]);
}
void mouse_pointer(BarnixPointer *out) {
    if(out){out->mouse=ps2_mouse_state;out->wheel=wheel_total;out->horizontal=horizontal_total;}
}
int mouse_source(unsigned int source,int connected,int dx,int dy,unsigned int buttons,int wheel,int horizontal) {
    if(source>=96)return 0;
    source_present[source]=!!connected;source_buttons[source]=connected?(buttons&31):0;
    unsigned int all_buttons=0;int available=0;
    for(int i=0;i<96;i++){all_buttons|=source_buttons[i];available|=source_present[i];}
    /* Bound untrusted HID deltas before adding to the logical cursor. */
    if(dx>640)dx=640;if(dx< -640)dx= -640;
    if(dy>400)dy=400;if(dy< -400)dy= -400;
    int x=ps2_mouse_state.x+dx,y=ps2_mouse_state.y+dy;
    if(x<0)x=0;if(x>639)x=639;if(y<0)y=0;if(y>399)y=399;
    int changed=x!=ps2_mouse_state.x||y!=ps2_mouse_state.y||all_buttons!=ps2_mouse_state.buttons||available!=ps2_mouse_state.available||wheel||horizontal;
    wheel_total=(int)((unsigned int)wheel_total+(unsigned int)wheel);
    horizontal_total=(int)((unsigned int)horizontal_total+(unsigned int)horizontal);
    ps2_mouse_state.x=x;ps2_mouse_state.y=y;ps2_mouse_state.buttons=all_buttons;
    ps2_mouse_state.available=available;
    if(changed)ps2_mouse_state.sequence++;
    return changed;
}
int mouse_relative(int dx,int dy,unsigned int buttons) {
    return mouse_source(0,1,dx,dy,buttons&7,0,0);
}

static unsigned char mouse_in(unsigned short port) {unsigned char v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;}
static void mouse_out(unsigned short port,unsigned char v) {__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));}
static int ready_write(void) {for(int i=0;i<100000;i++)if(!(mouse_in(0x64)&2))return 0;return -1;}
static int controller(unsigned char command) {if(ready_write())return -1;mouse_out(0x64,command);return 0;}
static int send_data(unsigned char data) {if(ready_write())return -1;mouse_out(0x60,data);return 0;}
static int response(int aux) {
    for(int i=0;i<200000;i++) {
        unsigned char status=mouse_in(0x64);
        if(status&1){int byte=mouse_in(0x60);if(!aux||(status&32))return byte;}
    }
    return -1;
}
static int mouse_command(unsigned char command) {
    for(int i=0;i<3;i++) {
        if(controller(0xd4)||send_data(command))return -1;
        int reply=response(1);if(reply==0xfa)return 0;if(reply!=0xfe)return -1;
    }
    return -1;
}
void mouse_init(void) {
    mouse_reset();
    if(controller(0xad))return; /* keyboard temporarily disabled during ACK exchange */
    for(int i=0;i<96&&(mouse_in(0x64)&1);i++)mouse_in(0x60);
    if(controller(0xa8)||controller(0x20))goto done;
    int config=response(0);if(config<0)goto done;
    /* IRQ handlers are not installed: both ports use the shared polling path. */
    if(controller(0x60)||send_data(config&~0x23))goto done;
    if(mouse_command(0xf6)||mouse_command(0xf4))goto done;
    mouse_source(0,1,0,0,0,0,0);
done:
    controller(0xae);
}
