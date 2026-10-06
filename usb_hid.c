#include "usb_hid.h"
#include "mouse.h"
#include <stdint.h>
/* HID 1.11 short items, global Push/Pop, report IDs and nested collections.
 * A bounded parser rejects malformed descriptors instead of guessing offsets. */
typedef struct { unsigned int page,size,count,id; int minimum,maximum; } Global;
static int signed_value(unsigned int v,unsigned int bytes){
    if(bytes&&bytes<4&&(v&(1U<<(bytes*8-1))))v|=~((1U<<(bytes*8))-1);
    return (int)v;
}
int hid_mouse_descriptor(HidMouse *m,const unsigned char *d,unsigned int length){
    if(!m||!d||!length||length>4096)return -1;
    *m=(HidMouse){0};Global g={0},saved[8];unsigned int sp=0,offsets[256]={0};
    unsigned int usages[64],used=0,umin=0,umax=0;int have_range=0;
    unsigned char collection[16]={0};unsigned int depth=0;int x=0,y=0;
    for(unsigned int p=0;p<length;){
        unsigned int prefix=d[p++];if(prefix==0xfe)return -1;
        unsigned int size=prefix&3;if(size==3)size=4;
        unsigned int type=(prefix>>2)&3,tag=prefix>>4,value=0;
        if(size>length-p)return -1;
        for(unsigned int i=0;i<size;i++)value|=(unsigned int)d[p++]<<(8*i);
        if(type==1){switch(tag){
            case 0:g.page=value;break;
            case 1:g.minimum=signed_value(value,size);break;
            case 2:g.maximum=g.minimum<0?signed_value(value,size):(int)value;break;
            case 7:if(value>32)return -1;g.size=value;break;
            case 8:if(!value||value>255)return -1;g.id=value;m->report_ids=1;break;
            case 9:if(value>512)return -1;g.count=value;break;
            case 10:if(sp==8)return -1;saved[sp++]=g;break;
            case 11:if(!sp)return -1;g=saved[--sp];break;
            default:break;
        }}else if(type==2){
            unsigned int usage=size==4?value:(g.page<<16)|value;
            if(tag==0){if(used==64)return -1;usages[used++]=usage;}
            else if(tag==1){umin=usage;have_range=1;}
            else if(tag==2)umax=usage;
        }else if(type==0){
            if(tag==10){if(depth==16)return -1;unsigned int u=used?usages[0]:umin;
                collection[depth]=(depth?collection[depth-1]:0)||(value==1&&(u==0x10002||u==0x10001));depth++;
            }else if(tag==12){if(!depth)return -1;depth--;
            }else if(tag==8){
                if(!g.size||g.size*g.count>4096-offsets[g.id])return -1;
                if(!(value&1)&&(value&2)&&depth&&collection[depth-1])for(unsigned int i=0;i<g.count;i++){
                    unsigned int u=used?usages[i<used?i:used-1]:have_range&&umin<=umax&&i<=umax-umin?umin+i:0;
                    unsigned int kind=0;
                    if(u>=0x90001&&u<=0x90005)kind=10+(u&0xffff);
                    if(u==0x10030){kind=1;x=1;}if(u==0x10031){kind=2;y=1;}
                    if(u==0x10038)kind=3;if(u==0xc0238)kind=4;
                    if(kind){if(m->count==HID_MOUSE_FIELDS)return -1;
                        HidMouseField *f=&m->fields[m->count++];
                        *f=(HidMouseField){offsets[g.id]+i*g.size,g.size,g.id,kind,!!(value&4),g.minimum,g.maximum};}
                }
                offsets[g.id]+=g.size*g.count;
            }
            used=0;umin=umax=0;have_range=0;
        }
    }
    if(depth||sp||!x||!y)return -1;
    if(m->report_ids)for(unsigned int i=0;i<m->count;i++)if(!m->fields[i].id)return -1;
    return 0;
}
static int value_at(const unsigned char *p,HidMouseField *f){
    unsigned int v=0;for(unsigned int i=0;i<f->size;i++)v|=((p[(f->bit+i)/8]>>((f->bit+i)%8))&1U)<<i;
    if(f->minimum<0&&f->size<32&&(v&(1U<<(f->size-1))))v|=~((1U<<f->size)-1);
    return (int)v;
}
static int absolute(int value,int low,int high,int pixels){
    if(high<=low)return 0;
    if(value<=low)return 0;if(value>=high)return pixels-1;
    unsigned int range=(unsigned int)high-(unsigned int)low,position=(unsigned int)value-(unsigned int)low;
    while(range>65535){range>>=1;position>>=1;}
    return (int)(position*(unsigned int)(pixels-1)/range);
}
int hid_mouse_report(HidMouse *m,unsigned int source,const unsigned char *p,unsigned int length){
    if(!m||!p||!length)return 0;
    unsigned int id=0;if(m->report_ids){id=*p++;length--;}
    int matched=0;
    for(unsigned int i=0;i<m->count;i++){HidMouseField *f=&m->fields[i];if(f->id==id){matched=1;if(f->bit+f->size>length*8)return 0;}}
    if(!matched)return 0;
    BarnixMouse current;mouse_get(&current);int dx=0,dy=0,wheel=0,pan=0;unsigned int buttons=m->buttons;
    for(unsigned int i=0;i<m->count;i++){HidMouseField *f=&m->fields[i];if(f->id!=id)continue;int v=value_at(p,f);
        if(f->kind>=11&&f->kind<=15){unsigned int bit=1U<<(f->kind-11);if(v)buttons|=bit;else buttons&=~bit;}
        else if(f->kind==1)dx=f->relative?v:absolute(v,f->minimum,f->maximum,640)-current.x;
        else if(f->kind==2)dy=f->relative?v:absolute(v,f->minimum,f->maximum,400)-current.y;
        else if(f->kind==3)wheel=v;else if(f->kind==4)pan=v;
    }
    m->buttons=buttons;return mouse_source(source,1,dx,dy,buttons,wheel,pan);
}
