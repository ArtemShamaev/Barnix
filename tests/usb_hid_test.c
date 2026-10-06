#include <assert.h>
#include <stdio.h>
#include "mouse.h"
#include "usb_hid.h"
static const unsigned char descriptor[]={
  0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,
  0x85,7,0x05,9,0x19,1,0x29,5,0x15,0,0x25,1,0x75,1,0x95,5,0x81,2,
  0x75,3,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,
  0x16,0,0x80,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,6,
  0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6,
  0x05,0x0c,0x0a,0x38,2,0x81,6,0xc0,0xc0};
int main(void){
 HidMouse m;assert(!hid_mouse_descriptor(&m,descriptor,sizeof(descriptor)));mouse_reset();
 unsigned char report[]={7,0x19,10,0,0xfb,0xff,1,0xff};
 assert(hid_mouse_report(&m,1,report,sizeof(report)));BarnixPointer p;mouse_pointer(&p);
 assert(p.mouse.x==330&&p.mouse.y==195&&p.mouse.buttons==0x19&&p.wheel==1&&p.horizontal==-1);
 unsigned int sequence=p.mouse.sequence;
 assert(!hid_mouse_report(&m,1,report,3));mouse_pointer(&p);assert(p.mouse.sequence==sequence);
 report[0]=8;assert(!hid_mouse_report(&m,1,report,sizeof(report)));
 mouse_source(2,1,0,0,2,0,0);mouse_source(1,0,0,0,0,0,0);mouse_pointer(&p);assert(p.mouse.buttons==2&&p.mouse.available);
 mouse_source(2,0,0,0,0,0,0);mouse_pointer(&p);assert(!p.mouse.buttons&&!p.mouse.available);
 for(unsigned int n=0;n<sizeof(descriptor);n++){HidMouse short_m;assert(hid_mouse_descriptor(&short_m,descriptor,n)<0);}
 unsigned char bad[]={0x75,33};assert(hid_mouse_descriptor(&m,bad,sizeof(bad))<0);

 /* Absolute tablets use the same report parser; endpoints clip to screen. */
 const unsigned char tablet[]={0x05,1,0x09,2,0xa1,1,0x09,0x30,0x09,0x31,
   0x15,0,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,2,0xc0};
 assert(!hid_mouse_descriptor(&m,tablet,sizeof(tablet)));mouse_reset();
 const unsigned char corner[]={0xff,0x7f,0,0};
 assert(hid_mouse_report(&m,1,corner,sizeof(corner)));mouse_pointer(&p);
 assert(p.mouse.x==639&&p.mouse.y==0);
 /* Buttons and motion in distinct reports must not release each other. */
 const unsigned char split[]={0x05,1,0x09,2,0xa1,1,0x85,1,
   0x05,9,0x19,1,0x29,5,0x15,0,0x25,1,0x75,1,0x95,5,0x81,2,
   0x75,3,0x95,1,0x81,1,0x85,2,0x05,1,0x09,0x30,0x09,0x31,
   0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6,0xc0};
 assert(!hid_mouse_descriptor(&m,split,sizeof(split)));mouse_reset();
 const unsigned char press[]={1,0x11},motion[]={2,3,0xfd},release[]={1,0};
 hid_mouse_report(&m,1,press,sizeof(press));hid_mouse_report(&m,1,motion,sizeof(motion));
 mouse_pointer(&p);assert(p.mouse.buttons==0x11&&p.mouse.x==323&&p.mouse.y==197);
 hid_mouse_report(&m,1,release,sizeof(release));mouse_pointer(&p);assert(!p.mouse.buttons);
 /* Signed axes may start at an arbitrary bit, including a 12-bit field. */
 const unsigned char packed[]={0x05,1,0x09,2,0xa1,1,0x75,1,0x95,1,0x81,1,
   0x09,0x30,0x09,0x31,0x16,0,0xf8,0x26,0xff,7,0x75,12,0x95,2,0x81,6,0xc0};
 assert(!hid_mouse_descriptor(&m,packed,sizeof(packed)));mouse_reset();
 unsigned int bits=(0xffdU<<1)|(5U<<13);unsigned char unaligned[]={bits,bits>>8,bits>>16,bits>>24};
 hid_mouse_report(&m,1,unaligned,sizeof(unaligned));mouse_pointer(&p);
 assert(p.mouse.x==317&&p.mouse.y==205);
 puts("HID: report IDs, 16-bit axes, five buttons, wheel, pan, truncation and independent devices passed");
}
