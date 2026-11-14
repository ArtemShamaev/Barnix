/* CPU Canvas2D: affine paths and source-over painting into the web display list. */
#include "web_js.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define POINTS 1024
#define STACK 32
typedef struct { double x,y; int start; } Point;
typedef struct { double m[6],alpha,width; unsigned int fill,stroke; } Pen;
struct WCanvasState { WJs *owner; WNode *node; WImage image; Pen pen,saved[STACK]; int depth,count; Point points[POINTS]; };
static void reset(WCanvasState *c) { memset(&c->pen,0,sizeof(c->pen));c->pen.m[0]=c->pen.m[3]=1;c->pen.alpha=c->pen.width=1;c->pen.fill=c->pen.stroke=0xff000000;c->depth=c->count=0; }
static int resize(WCanvasState *c) {
    const char *a=w_attr(c->node,"width"),*b=w_attr(c->node,"height");
    unsigned long ww=a?strtoul(a,0,10):300,hh=b?strtoul(b,0,10):150;
    if(ww>2048||hh>2048)return -1;
    int w=(int)ww,h=(int)hh;
    if(w<0||h<0||w>2048||h>2048) return -1;
    if(w!=c->image.width||h!=c->image.height||!c->image.pixels) {
        unsigned int old=(unsigned int)c->image.width*c->image.height*4, bytes=(unsigned int)w*h*4;
        if(bytes> (16u<<20) - (c->owner->canvas_bytes-old))return -1;
        unsigned int *p=calloc((size_t)(w?w:1)*(h?h:1),4);if(!p)return -1;
        c->owner->canvas_bytes=c->owner->canvas_bytes-old+bytes;
        free(c->image.pixels);c->image.pixels=p;c->image.width=w;c->image.height=h;
    } else memset(c->image.pixels,0,(size_t)w*h*4);
    reset(c);c->node->image=&c->image;return 0;
}
static double bounded(double x) { return x>100000?100000:x< -100000?-100000:x; }
static void point(WCanvasState *c,double x,double y,int start) {
    if(c->count>=POINTS)return;
    double *m=c->pen.m;Point *p=&c->points[c->count++];
    p->x=bounded(x*m[0]+y*m[2]+m[4]);p->y=bounded(x*m[1]+y*m[3]+m[5]);p->start=start;
}
static void pixel(WCanvasState *c,int x,int y,unsigned int color,int clear) {
    if(x<0||y<0||x>=c->image.width||y>=c->image.height)return;
    if(!c->owner->draw_budget)return;
    c->owner->draw_budget--;
    unsigned int *p=c->image.pixels+y*c->image.width+x;
    if(clear){*p=0;return;}
    unsigned int a=(unsigned int)((color>>24)*c->pen.alpha),old=*p,oa=old>>24;
    unsigned int na=a+oa*(255-a)/255;
    if(!na){*p=0;return;}
    unsigned int out=na<<24;
    for(int shift=0;shift<24;shift+=8) {
        unsigned int v=(((color>>shift)&255)*a+((old>>shift)&255)*oa*(255-a)/255)/na;
        out|=v<<shift;
    }
    *p=out;
}
static void fill(WCanvasState *c,unsigned int color,int clear) {
    if(c->count<3)return;
    int low=c->image.height,high=0;
    for(int i=0;i<c->count;i++){int y=(int)floor(c->points[i].y);if(y<low)low=y;if(y+1>high)high=y+1;}
    if(low<0)low=0;
    if(high>c->image.height)high=c->image.height;
    double xs[POINTS];
    for(int y=low;y<high && c->owner->draw_budget;y++) {
        int count=0,start=0;
        for(int i=0;i<c->count;i++) {
            if(c->points[i].start)start=i;
            int next=i+1;if(next==c->count||c->points[next].start)next=start;
            Point p=c->points[i],q=c->points[next];double yy=y+0.5;
            if((p.y<=yy&&q.y>yy)||(q.y<=yy&&p.y>yy)) {
                double x=p.x+(yy-p.y)*(q.x-p.x)/(q.y-p.y);int k=count++;
                while(k>0&&xs[k-1]>x){xs[k]=xs[k-1];k--;}xs[k]=x;
            }
        }
        for(int i=0;i+1<count;i+=2) {
            int a=(int)ceil(xs[i]-0.5),b=(int)ceil(xs[i+1]-0.5);
            if(a<0)a=0;
            if(b>c->image.width)b=c->image.width;
            for(int x=a;x<b;x++)pixel(c,x,y,color,clear);
        }
    }
}
static void line(WCanvasState *c,Point a,Point b) {
    /* Clip first, so coordinates outside the canvas cannot cause huge loops. */
    double dx=b.x-a.x,dy=b.y-a.y,t0=0,t1=1;
    double p[4]={-dx,dx,-dy,dy},q[4]={a.x+32,c->image.width+32-a.x,a.y+32,c->image.height+32-a.y};
    for(int k=0;k<4;k++){if(p[k]==0){if(q[k]<0)return;}else {double t=q[k]/p[k];if(p[k]<0){if(t>t1)return;if(t>t0)t0=t;}else{if(t<t0)return;if(t<t1)t1=t;}}}
    b.x=a.x+dx*t1;b.y=a.y+dy*t1;a.x+=dx*t0;a.y+=dy*t0;
    dx=b.x-a.x;dy=b.y-a.y;int steps=(int)ceil(fmax(fabs(dx),fabs(dy)));
    if(steps<1)steps=1;
    int r=(int)(c->pen.width/2);if(r>32)r=32;
    for(int i=0;i<=steps && c->owner->draw_budget;i++) {int x=(int)floor(a.x+dx*i/steps),y=(int)floor(a.y+dy*i/steps);
        for(int v=-r;v<=r;v++)for(int u=-r;u<=r;u++)if(u*u+v*v<=r*r)pixel(c,x+u,y+v,c->pen.stroke,0);
    }
}
static void stroke(WCanvasState *c) { for(int i=1;i<c->count;i++)if(!c->points[i].start)line(c,c->points[i-1],c->points[i]); }
static void multiply(Pen *p,double *b) {
    double a[6];memcpy(a,p->m,sizeof(a));
    p->m[0]=a[0]*b[0]+a[2]*b[1];p->m[1]=a[1]*b[0]+a[3]*b[1];
    p->m[2]=a[0]*b[2]+a[2]*b[3];p->m[3]=a[1]*b[2]+a[3]*b[3];
    p->m[4]=a[0]*b[4]+a[2]*b[5]+a[4];p->m[5]=a[1]*b[4]+a[3]*b[5]+a[5];
    for(int i=0;i<6;i++)p->m[i]=bounded(p->m[i]);
}
JSValue web_canvas_call(WJs *j,WNode *n,const char *op,int argc,JSValueConst *argv) {
    JSContext *ctx=j->ctx;if(n->tag!=T_CANVAS)return JS_ThrowTypeError(ctx,"Not a canvas");
    WCanvasState *c=0;for(int i=0;i<j->canvas_count;i++)if(j->canvases[i]->node==n)c=j->canvases[i];
    if(!c) {
        if(j->canvas_count==WJS_CANVASES)return JS_ThrowRangeError(ctx,"Too many canvases");
        c=calloc(1,sizeof(*c));if(!c)return JS_ThrowOutOfMemory(ctx);c->node=n;c->owner=j;
        if(resize(c)){free(c);return JS_ThrowRangeError(ctx,"Canvas dimensions exceed memory limit (2048 x 2048)");}
        j->canvases[j->canvas_count++]=c;
    }
    double a[6]={0};
    if(!w_eq(op,"get")&&!w_eq(op,"set")&&!w_eq(op,"setLineDash"))for(int i=0;i<argc&&i<6;i++) {
        if(JS_ToFloat64(ctx,&a[i],argv[i]))return JS_EXCEPTION;
        if(!isfinite(a[i]))return JS_UNDEFINED;
        a[i]=bounded(a[i]);
    }
    if(w_eq(op,"init"))return JS_UNDEFINED;
    if(w_eq(op,"resize")){if(resize(c))return JS_ThrowRangeError(ctx,"Canvas is too large");}
    else if(w_eq(op,"get")||w_eq(op,"set")) {
        if(argc<1)return JS_UNDEFINED;
        const char *key=JS_ToCString(ctx,argv[0]);if(!key)return JS_EXCEPTION;
        JSValue r=JS_UNDEFINED;
        if(w_eq(op,"get")) {
            if(w_eq(key,"globalAlpha"))r=JS_NewFloat64(ctx,c->pen.alpha);
            else if(w_eq(key,"lineWidth"))r=JS_NewFloat64(ctx,c->pen.width);
            else if(w_eq(key,"globalCompositeOperation"))r=JS_NewString(ctx,"source-over");
            else {char color[8]="#000000";unsigned int v=w_eq(key,"fillStyle")?c->pen.fill:c->pen.stroke;for(int i=0;i<6;i++)color[i+1]="0123456789abcdef"[(v>>(20-i*4))&15];r=JS_NewString(ctx,color);}
        } else if(argc>1) {
            if(w_eq(key,"globalAlpha")||w_eq(key,"lineWidth")) {
                double v;if(!JS_ToFloat64(ctx,&v,argv[1])&&isfinite(v)){if(w_eq(key,"globalAlpha")&&v>=0&&v<=1)c->pen.alpha=v;else if(w_eq(key,"lineWidth")&&v>0)c->pen.width=v;}
            } else {
                const char *s=JS_ToCString(ctx,argv[1]);if(s){int ok=0;unsigned int color=w_parse_color(s,0xff000000,&ok);
                    if(w_eq(key,"globalCompositeOperation")){if(!w_eq(s,"source-over"))r=JS_ThrowTypeError(ctx,"Only source-over compositing is supported");}
                    else if(ok){if(w_eq(key,"fillStyle"))c->pen.fill=color;else c->pen.stroke=color;}
                    JS_FreeCString(ctx,s);
                }
            }
        }
        JS_FreeCString(ctx,key);return r;
    }
    else if(w_eq(op,"save")){if(c->depth==STACK)return JS_ThrowRangeError(ctx,"Canvas state stack is full");c->saved[c->depth++]=c->pen;}
    else if(w_eq(op,"restore")){if(c->depth)c->pen=c->saved[--c->depth];}
    else if(w_eq(op,"setTransform")){if(argc>=6)memcpy(c->pen.m,a,sizeof(a));}
    else if(w_eq(op,"resetTransform")){memset(c->pen.m,0,sizeof(a));c->pen.m[0]=c->pen.m[3]=1;}
    else if(w_eq(op,"transform")){if(argc>=6)multiply(&c->pen,a);}
    else if(w_eq(op,"translate")){double b[6]={1,0,0,1,a[0],a[1]};multiply(&c->pen,b);}
    else if(w_eq(op,"scale")){double b[6]={a[0],0,0,a[1],0,0};multiply(&c->pen,b);}
    else if(w_eq(op,"rotate")){double b[6]={cos(a[0]),sin(a[0]),-sin(a[0]),cos(a[0]),0,0};multiply(&c->pen,b);}
    else if(w_eq(op,"beginPath"))c->count=0;
    else if(w_eq(op,"moveTo")||w_eq(op,"lineTo")){if(c->count==POINTS)return JS_ThrowRangeError(ctx,"Canvas path is too large");point(c,a[0],a[1],w_eq(op,"moveTo")||!c->count);}
    else if(w_eq(op,"closePath")){int first=c->count-1;while(first>0&&!c->points[first].start)first--;if(c->count&&c->count<POINTS){Point p=c->points[first];p.start=0;c->points[c->count++]=p;}}
    else if(w_eq(op,"rect")||w_eq(op,"fillRect")||w_eq(op,"clearRect")||w_eq(op,"strokeRect")) {
        int keep=w_eq(op,"rect");Point old[POINTS];int count=c->count;
        if(!keep){memcpy(old,c->points,sizeof(old));c->count=0;}
        point(c,a[0],a[1],1);point(c,a[0]+a[2],a[1],0);point(c,a[0]+a[2],a[1]+a[3],0);point(c,a[0],a[1]+a[3],0);point(c,a[0],a[1],0);
        if(!keep){if(w_eq(op,"strokeRect"))stroke(c);else fill(c,c->pen.fill,w_eq(op,"clearRect"));memcpy(c->points,old,sizeof(old));c->count=count;}
    }
    else if(w_eq(op,"fill"))fill(c,c->pen.fill,0);
    else if(w_eq(op,"stroke"))stroke(c);
    else if(w_eq(op,"arc")) {
        if(a[2]<0)return JS_ThrowRangeError(ctx,"Negative arc radius");
        double delta=a[4]-a[3],tau=6.283185307179586;int ccw=argc>5&&JS_ToBool(ctx,argv[5]);
        if(!ccw){if(delta>=tau)delta=tau;else{delta=fmod(delta,tau);if(delta<0)delta+=tau;}}
        else {if(-delta>=tau)delta=-tau;else{delta=fmod(delta,tau);if(delta>0)delta-=tau;}}
        int steps=(int)(fabs(delta)*16)+1;
        if(c->count+steps+1>=POINTS)return JS_ThrowRangeError(ctx,"Canvas path is too large");
        for(int i=0;i<=steps;i++){double t=a[3]+delta*i/steps;point(c,a[0]+a[2]*cos(t),a[1]+a[2]*sin(t),!c->count);}
    } else if(w_eq(op,"setLineDash")) {
        if(argc) { JSValue len=JS_GetPropertyStr(ctx,argv[0],"length"); unsigned int count=0;JS_ToUint32(ctx,&count,len);JS_FreeValue(ctx,len);
            if(count)return JS_ThrowTypeError(ctx,"Dashed strokes are not supported yet"); }
    }
    else return JS_ThrowTypeError(ctx,"Unsupported canvas operation: %s",op);
    if(!j->draw_budget){j->stopped=1;return JS_ThrowRangeError(ctx,"Canvas frame work limit exceeded");}
    j->dirty=1;return JS_UNDEFINED;
}
void web_canvas_free(WJs *j) {for(int i=0;i<j->canvas_count;i++){free(j->canvases[i]->image.pixels);free(j->canvases[i]);}}
