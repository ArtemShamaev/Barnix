/* Exercise the real browser engine and QuickJS, including allocation lifetime. */
#define main web_render_main
#include "web_render.c"
#undef main
#include "apps/gouse/web_js.h"
#include <assert.h>
static unsigned char arena_memory[128u << 20];
static WDoc *page(const char *html) {
    web_arena(arena_memory,sizeof(arena_memory));
    WDoc *d=web_parse(html,(int)strlen(html),"file:///tmp/test.html");
    web_load_resources(d,0);web_layout(d,640,480);
    if(web_js_error(d)[0])fprintf(stderr,"JS: %s\n",web_js_error(d));
    return d;
}
static WNode *find(WNode *n,const char *id) {
    if(n->id&&!strcmp(n->id,id))return n;
    for(WNode *c=n->first;c;c=c->next){WNode *r=find(c,id);if(r)return r;}return 0;
}
static void text_is(WNode *n,const char *s) {char buf[128];assert(n);web_text_of(n,buf,sizeof(buf));if(strcmp(buf,s))fprintf(stderr,"text \"%s\", expected \"%s\"\n",buf,s);assert(!strcmp(buf,s));}
int main(void) {
    WDoc *d=page("<body><p id='out'>old</p><button id='b' onclick=\"document.getElementById('out').textContent='clicked'\">Click</button><canvas id='c' width='64' height='32'></canvas><script>class Counter { constructor(){this.v=7;} } const c=new Counter(); const a=new Uint8Array([1,2,3]); document.getElementById('out').textContent=`value:${c.v+a[2]}`; const ctx=document.getElementById('c').getContext('2d');ctx.fillStyle='#ff0000';ctx.fillRect(1,2,10,8);if(document.getElementById('c').getContext('webgl')!==null)throw Error('false WebGL support');setTimeout(()=>{document.getElementById('out').textContent='timer';},20);requestAnimationFrame(()=>{ctx.fillStyle='#00ff00';ctx.fillRect(20,2,10,8);});Promise.resolve().then(()=>{document.title='promise';});</script>");
    assert(!web_js_error(d)[0]);assert(!strcmp(d->title,"promise"));text_is(find(d->root,"out"),"value:10");
    WNode *c=find(d->root,"c");assert(c->image && c->image->pixels[3*64+2]==0xffff0000);
    assert(web_js_tick(d,30,640,480));web_layout(d,640,480);
    text_is(find(d->root,"out"),"timer");assert(c->image->pixels[3*64+22]==0xff00ff00);
    web_js_event(d,find(d->root,"b"),"click",0,0,0);web_layout(d,640,480);text_is(find(d->root,"out"),"clicked");
    assert(!web_js_error(d)[0]);
    for(int i=0;i<100;i++){web_js_tick(d,50+i*16,640,480);web_layout(d,640,480);}
    assert(!web_js_error(d)[0]);web_js_destroy(d);
    /* Cancellation must also work for callbacks already selected for this frame. */
    d=page("<script>let calls=[];const canceled=requestAnimationFrame(()=>calls.push('canceled'));setTimeout(()=>cancelAnimationFrame(canceled),0);requestAnimationFrame(()=>{calls.push('first');cancelAnimationFrame(second);requestAnimationFrame(()=>calls.push('next'));});const second=requestAnimationFrame(()=>calls.push('second'));</script>");
    web_js_tick(d,16,640,480);web_layout(d,640,480);
    JSValue result=JS_Eval(((WJs *)d->js)->ctx,"calls.join(',')",15,"test",JS_EVAL_TYPE_GLOBAL);
    const char *value=JS_ToCString(((WJs *)d->js)->ctx,result);assert(value&&!strcmp(value,"first"));
    JS_FreeCString(((WJs *)d->js)->ctx,value);JS_FreeValue(((WJs *)d->js)->ctx,result);
    web_js_tick(d,32,640,480);web_layout(d,640,480);
    result=JS_Eval(((WJs *)d->js)->ctx,"calls.join(',')",15,"test",JS_EVAL_TYPE_GLOBAL);
    value=JS_ToCString(((WJs *)d->js)->ctx,result);assert(value&&!strcmp(value,"first,next"));
    JS_FreeCString(((WJs *)d->js)->ctx,value);JS_FreeValue(((WJs *)d->js)->ctx,result);
    assert(!web_js_error(d)[0]);web_js_destroy(d);
    /* Pending microtasks keep an otherwise idle page alive across budget slices. */
    d=page("<script>let steps=0;function step(){if(++steps<1200)queueMicrotask(step);else document.title='microtasks done';}queueMicrotask(step);</script>");
    for(int i=0;i<10;i++){web_js_tick(d,16+i*16,640,480);web_layout(d,640,480);}
    assert(!web_js_error(d)[0]);assert(!strcmp(d->title,"microtasks done"));web_js_destroy(d);
    d=page("<p id='out'>ok</p><script>while(true){}</script>");
    assert(strstr(web_js_error(d),"interrupted"));web_js_destroy(d);
    /* Navigation creates a fresh realm after an interrupted page. */
    d=page("<script>document.title=typeof c</script>");assert(!strcmp(d->title,"undefined"));web_js_destroy(d);
    char path[1024];assert(getcwd(path,sizeof(path)));strcat(path,"/apps/gouse/examples/three-game.html");
    web_arena(arena_memory,sizeof(arena_memory));WebResource r;assert(!web_fetch(path,&r,WEB_RESOURCE_MAX,0,0));
    d=web_parse(r.data,r.length,path);web_load_resources(d,0);web_layout(d,640,480);
    if(web_js_error(d)[0])fprintf(stderr,"Three.js: %s\n",web_js_error(d));assert(!web_js_error(d)[0]);
    WNode *game=find(d->root,"game");assert(game&&game->first&&game->first->image);
    WImage *im=game->first->image;int colored=0;for(int i=0;i<im->width*im->height;i++)if((im->pixels[i]&0xffffff)!=0x172238)colored++;
    assert(colored>100);for(int i=0;i<10;i++){web_js_tick(d,100+i*16,640,480);web_layout(d,640,480);}
    assert(!web_js_error(d)[0]);web_js_destroy(d);
    /* A timer that is not due yet: no call into JavaScript, the layout stays; a due one changes the DOM. */
    d=page("<style>li::before{content:'*'}</style><ul id='l'><li class='a'>1</li><li class='b x'>2</li><li class='b'>3</li></ul><p id='out'></p><script>"
           "const o=document.getElementById('out'),l=document.getElementById('l');"
           "o.textContent=[document.querySelectorAll('ul > li.b').length,document.querySelector('li.b.x').textContent,l.children.length,l.firstChild.nodeName,"
           "document.getElementsByClassName('b').length,document.querySelectorAll('li:not(.a), p').length,document.querySelectorAll('li:bogus(').length].join(',');"
           "const n=document.createElement('li');n.textContent='0';l.insertBefore(n,l.firstChild);"
           "setTimeout(()=>{o.textContent='late';},1000);</script>");
    assert(!web_js_error(d)[0]);text_is(find(d->root,"out"),"2,2,3,li,2,3,0");
    text_is(find(d->root,"l"),"0123");
    int items=d->item_count;
    assert(web_js_tick(d,500,640,480)==0 && d->item_count==items);
    assert(web_js_tick(d,1000,640,480)==2);web_layout(d,640,480);text_is(find(d->root,"out"),"late");
    assert(web_js_tick(d,1100,640,480)==0);web_js_destroy(d);
    /* More nodes than the old fixed table held. */
    d=page("<div id='box'></div><script>const b=document.getElementById('box');for(let i=0;i<12000;i++)b.appendChild(document.createElement('span'));"
           "document.title=String(document.querySelectorAll('#box span').length)+':'+b.lastChild.nodeName;</script>");
    assert(!web_js_error(d)[0]);assert(!strcmp(d->title,"12000:span"));web_js_destroy(d);
    puts("QuickJS: modern JS, promises, DOM, events, timers, canvas, interruption, navigation and real Three.js rendering passed");return 0;
}
