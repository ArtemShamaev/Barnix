#include <barnix_graphics.h>
#include "text.h"
/* Render a bounded number of glyphs, never a partial UTF-8 byte prefix. */
static void text_columns(BglApp *a,int x,int y,const char *s,int fg,int bg,int columns){
    Utf8Decoder decoder={0,0,0};
    if(!a||!s)return;
    while((*s||decoder.remaining)&&columns>0&&x<640){
        unsigned int out[2];int count;
        if(*s)count=utf8_feed(&decoder,(unsigned char)*s++,out);
        else {out[0]='?';count=1;decoder.remaining=0;}
        for(int k=0;k<count&&columns>0;k++,columns--,x+=8){
            unsigned int c=text_glyph(out[k]);
            if(x>=0&&y>=0&&y<400)a->cells[(y/16)*80+x/8]=((bg&15)<<12)|((fg&15)<<8)|c;
            for(int j=0;j<16;j++){
                unsigned int bits=barnix->glyph(c,j);
                for(int i=0;i<8;i++)if(x+i>=0&&x+i<640&&y+j>=0&&y+j<400)
                    a->pixels[(y+j)*640+x+i]=(bits&(128U>>i)?fg:bg)&15;
            }
        }
    }
}
static int limit(int v,int low,int high){return v<low?low:v>high?high:v;}
static int inside(int x,int y,int rx,int ry,int w,int h){return x>=rx&&y>=ry&&x-rx<w&&y-ry<h;}
void bgl_fill(BglApp *a,int x,int y,int w,int h,int color){
    if(!a||w<=0||h<=0)return;
    long long right=(long long)x+w,bottom=(long long)y+h;
    if(x>=640||y>=400||right<=0||bottom<=0)return;
    int ex=right>640?640:(int)right,ey=bottom>400?400:(int)bottom;
    x=limit(x,0,640);y=limit(y,0,400);
    for(int j=y;j<ey;j++)for(int i=x;i<ex;i++)a->pixels[j*640+i]=color&15;
    for(int j=y/16;j<(ey+15)/16;j++)for(int i=x/8;i<(ex+7)/8;i++)a->cells[j*80+i]=((color&15)<<12)|0x20;
}
void bgl_text(BglApp *a,int x,int y,const char *s,int fg,int bg){
    if(!a||!s)return;
    text_columns(a,x,y,s,fg,bg,80);
}
static void bevel(BglApp *a,int x,int y,int w,int h,int pressed){
    bgl_fill(a,x,y,w,h,7);bgl_fill(a,x,y,w,1,pressed?8:15);bgl_fill(a,x,y,1,h,pressed?8:15);
    bgl_fill(a,x,y+h-1,w,1,pressed?15:8);bgl_fill(a,x+w-1,y,1,h,pressed?15:8);
}
void bgl_init(BglApp *a,const char *title,int x,int y,int w,int h){
    if(!a)return;
    a->w=limit(w,160,624);a->h=limit(h,96,368);a->x=limit(x,0,640-a->w);a->y=limit(y,16,400-a->h);
    a->desktop_title="Barnix Graphic Library";a->hint="Tab: focus  Enter: activate  Esc: close";
    a->title=title?title:"Barnix Graphic Library";a->drag=0;a->focus=-1;a->capture=-1;a->count=0;
    barnix->pointer(&a->previous);a->pixel_mode=0;
}
static int add(BglApp *a,int type,int id,int x,int y,int w,int h,const char *text,const char *const *items,int count){
    if(!a||a->count>=BGL_MAX_WIDGETS||x<4||y<24||w<16||h<16||x>a->w-w-4||y>a->h-h-4||count<0)return -1;
    for(int i=0;i<a->count;i++)if(a->widgets[i].id==id)return -1;
    BglWidget *v=&a->widgets[a->count++];*v=(BglWidget){type,id,x,y,w,h,0,0,count,text,items};
    if(a->focus<0&&type!=BGL_LABEL)a->focus=a->count-1;
    return 0;
}
int bgl_button(BglApp *a,int id,int x,int y,int w,const char *text){return add(a,BGL_BUTTON,id,x,y,w,24,text,0,0);}
int bgl_list(BglApp *a,int id,int x,int y,int w,int h,const char *const *items,int count){
    if(count&&!items)return -1;
    return add(a,BGL_LIST,id,x,y,w,h,0,items,count);
}
int bgl_label(BglApp *a,int id,int x,int y,int w,const char *text){return add(a,BGL_LABEL,id,x,y,w,16,text,0,0);}
static void select_item(BglWidget *v,int selected){
    v->selected=limit(selected,0,v->count?v->count-1:0);
    int rows=v->h/16;if(rows<1)rows=1;
    if(v->selected<v->first)v->first=v->selected;
    if(v->selected>=v->first+rows)v->first=v->selected-rows+1;
}
BglEvent bgl_poll(BglApp *a){
    BglEvent e={0,0,0,0};if(!a)return e;
    e.key=barnix->poll_key();BarnixPointer p;barnix->pointer(&p);
    int down=(p.mouse.buttons&1)&&!(a->previous.mouse.buttons&1),up=!(p.mouse.buttons&1)&&(a->previous.mouse.buttons&1);
    int x=p.mouse.x-a->x,y=p.mouse.y-a->y;
    if(e.key==KEY_ESCAPE)e.type=BGL_EVENT_CLOSE;
    if(e.key=='\t'&&a->count){int next=a->focus;for(int i=0;i<a->count;i++){next=(next+1)%a->count;if(a->widgets[next].type!=BGL_LABEL){a->focus=next;break;}}}
    if(down){a->capture=-1;
        if(inside(x,y,a->w-24,2,22,20))a->capture=-2;
        else if(inside(x,y,0,0,a->w,22)){a->drag=1;a->drag_x=x;a->drag_y=y;}
        else for(int i=a->count-1;i>=0;i--){BglWidget *v=&a->widgets[i];if(v->type!=BGL_LABEL&&inside(x,y,v->x,v->y,v->w,v->h)){
            a->focus=i;a->capture=i;
            if(v->type==BGL_LIST&&v->count){select_item(v,v->first+(y-v->y)/16);e.type=BGL_EVENT_SELECT;e.id=v->id;e.value=v->selected;}break;}}
    }
    if(a->drag&&(p.mouse.buttons&1)){a->x=limit(p.mouse.x-a->drag_x,0,640-a->w);a->y=limit(p.mouse.y-a->drag_y,16,400-a->h);}
    if(up){a->drag=0;
        if(a->capture==-2&&inside(x,y,a->w-24,2,22,20))e.type=BGL_EVENT_CLOSE;
        if(a->capture>=0){BglWidget *v=&a->widgets[a->capture];if(v->type==BGL_BUTTON&&inside(x,y,v->x,v->y,v->w,v->h)){e.type=BGL_EVENT_CLICK;e.id=v->id;}}
        a->capture=-1;
    }
    if(!p.mouse.available){a->drag=0;a->capture=-1;}
    if(a->focus>=0){BglWidget *v=&a->widgets[a->focus];
        int wheel=(int)((unsigned int)p.wheel-(unsigned int)a->previous.wheel);
        if(v->type==BGL_LIST&&v->count){int delta=e.key==KEY_DOWN?1:e.key==KEY_UP?-1:e.key==KEY_HOME?-v->count:e.key==KEY_END?v->count:0;
            if(inside(x,y,v->x,v->y,v->w,v->h))delta-=limit(wheel,-100,100);
            if(delta){select_item(v,v->selected+delta);e.type=BGL_EVENT_SELECT;e.id=v->id;e.value=v->selected;}}
        if(e.key=='\n'||(e.key==' '&&v->type==BGL_BUTTON)){e.type=BGL_EVENT_CLICK;e.id=v->id;e.value=v->selected;}
    }
    a->previous=p;return e;
}
void bgl_draw(BglApp *a){
    if(!a)return;
    bgl_fill(a,0,0,640,400,3);bgl_fill(a,0,0,640,16,7);bgl_text(a,8,0,a->desktop_title,0,7);
    bgl_fill(a,a->x+6,a->y+6,a->w,a->h,0);bevel(a,a->x,a->y,a->w,a->h,0);
    bgl_fill(a,a->x+2,a->y+2,a->w-4,20,1);
    text_columns(a,a->x+8,a->y+4,a->title,15,1,(a->w-40)/8);
    bgl_text(a,a->x+a->w-20,a->y+4,"X",15,1);
    for(int i=0;i<a->count;i++){BglWidget *v=&a->widgets[i];int x=a->x+v->x,y=a->y+v->y;
        if(v->type==BGL_BUTTON){bevel(a,x,y,v->w,v->h,a->capture==i);
            text_columns(a,x+8,y+4,v->text,a->focus==i?1:0,7,(v->w-16)/8);
        }else if(v->type==BGL_LABEL){text_columns(a,x,y,v->text,0,7,v->w/8);
        }else{bgl_fill(a,x,y,v->w,v->h,15);
            for(int row=0;row<v->h/16&&v->first+row<v->count;row++){int index=v->first+row,bg=index==v->selected?1:15,fg=index==v->selected?15:0;
                bgl_fill(a,x,y+row*16,v->w,16,bg);
                text_columns(a,x+4,y+row*16,v->items[index],fg,bg,(v->w-8)/8);}}

    }
    bgl_text(a,8,384,a->hint,15,3);
    if(a->previous.mouse.available){int x=a->previous.mouse.x,y=a->previous.mouse.y;
        for(int j=0;j<12;j++)for(int i=0;i<=j/2;i++)if(x+i>=0&&x+i<640&&y+j>=0&&y+j<400)a->pixels[(y+j)*640+x+i]=(i==0||i==j/2)?0:15;
    }
    a->pixel_mode=barnix->graphics(a->pixels)==0;
    if(!a->pixel_mode){if(a->previous.mouse.available&&inside(a->previous.mouse.x,a->previous.mouse.y,0,0,640,400))a->cells[(a->previous.mouse.y/16)*80+a->previous.mouse.x/8]^=0x7700;barnix->screen(a->cells);}
}
void bgl_close(BglApp *a){(void)a;barnix->graphics(0);barnix->clear();}
