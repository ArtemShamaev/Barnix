/* QuickJS integration. Each navigation owns a realm; no OS module is exposed. */
#include "web_js.h"
#include <stdlib.h>
#include <string.h>
#include "web_bootstrap.h"

static void exception(WJs *j) {
    JSValue e = JS_GetException(j->ctx);
    const char *s = JS_ToCString(j->ctx, e);
    w_copy(j->error, s ? s : "JavaScript exception", sizeof(j->error));
    if (JS_IsError(j->ctx, e)) {   /* where: the first frame of the stack */
        JSValue stack = JS_GetPropertyStr(j->ctx, e, "stack");
        const char *t = JS_IsString(stack) ? JS_ToCString(j->ctx, stack) : 0;
        const char *at = t ? strstr(t, "at ") : 0;
        if (at) {
            char where[160]; int k = 0;
            for (at += 3; *at && *at != '\n' && k < (int)sizeof(where) - 1; at++) where[k++] = *at;
            where[k] = 0;
            w_append(j->error, " (", sizeof(j->error)); w_append(j->error, where, sizeof(j->error)); w_append(j->error, ")", sizeof(j->error));
        }
        JS_FreeCString(j->ctx, t); JS_FreeValue(j->ctx, stack);
    }
    web_progress(j->error);
    JS_FreeCString(j->ctx, s); JS_FreeValue(j->ctx, e);
}
static int interrupted(JSRuntime *rt, void *opaque) {
    (void)rt; WJs *j = opaque;
    if (web_idle() || --j->budget <= 0) { j->stopped = 1; return 1; }
    if (j->deadline && (int)(web_time() - j->deadline) > 0) { j->overran = 1; return 1; }
    return 0;
}
static int id(WJs *j, WNode *n) {
    while (n && n->pseudo) n = n->parent;   /* ::before/::after live only as long as the layout */
    if (!n) return 0;
    if (n->js_id > 0 && n->js_id <= j->count && j->nodes[n->js_id] == n) return n->js_id;
    if (j->count + 1 >= j->capacity) {
        int capacity = j->capacity ? j->capacity * 2 : 1024;
        if (capacity > WJS_NODES) return 0;
        WNode **grown = realloc(j->nodes, sizeof(*grown) * (unsigned)capacity);
        if (!grown) return 0;
        j->nodes = grown; j->capacity = capacity;
    }
    j->nodes[++j->count] = n; n->js_id = j->count; return j->count;
}
/* The layout's memory lies above the page: it goes before the DOM changes or grows. */
static void writable(WJs *j) { if (!j->unlaid) { web_unlayout(j->doc); j->unlaid = 1; } }
static WNode *real(WNode *n) { while (n && n->pseudo) n = n->next; return n; }
static JSValue node_array(WJs *j, WNode **list, int count) {
    JSValue a = JS_NewArray(j->ctx);
    for (int i = 0, k = 0; i < count; i++) { int h = id(j, list[i]); if (h) JS_SetPropertyUint32(j->ctx, a, (uint32_t)k++, JS_NewInt32(j->ctx, h)); }
    return a;
}
static WNode *node(WJs *j, JSValueConst v) {
    int n = 0; if (JS_ToInt32(j->ctx, &n, v)) return 0;
    return n > 0 && n <= j->count ? j->nodes[n] : 0;
}
static void unlink_node(WNode *n) {
    if (!n->parent) return;
    if (n->prev) n->prev->next = n->next; else n->parent->first = n->next;
    if (n->next) n->next->prev = n->prev; else n->parent->last = n->prev;
    n->parent = n->next = n->prev = 0;
}
static void append_node(WNode *p, WNode *n) {
    unlink_node(n); n->parent = p; n->prev = p->last;
    if (p->last) p->last->next = n; else p->first = n;
    p->last = n;
}
static void attr(WNode *n, const char *key, const char *value) {
    WAttr *a; for (a = n->attrs; a; a = a->next) if (w_eq(a->name, key)) break;
    if (!a) { a = web_alloc(sizeof(*a)); a->name = web_strdup(key, -1); a->next = n->attrs; n->attrs = a; }
    if (a->value && w_eq(a->value,value)) return;
    a->value = web_strdup(value, -1);
    if (w_eq(key, "id")) n->id = a->value;
    if (w_eq(key, "class")) n->class_list = a->value;
}
static int same_origin(const char *a, const char *b) {
    const char *x = strstr(a, "://"), *y = strstr(b, "://");
    if (!x || !y) return 0;
    x = strchr(x + 3, '/'); y = strchr(y + 3, '/');
    size_t nx = x ? (size_t)(x - a) : strlen(a), ny = y ? (size_t)(y - b) : strlen(b);
    return nx == ny && !strncmp(a, b, nx);
}
static JSValue bridge(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; WJs *j = JS_GetContextOpaque(ctx);
    if (argc < 1) return JS_UNDEFINED;
    const char *op = JS_ToCString(ctx, argv[0]); if (!op) return JS_EXCEPTION;
    WNode *n = argc > 1 ? node(j, argv[1]) : 0;
    const char *key = argc > 2 ? JS_ToCString(ctx, argv[2]) : 0;
    JSValue r = JS_UNDEFINED;
    if (w_eq(op,"root")) r = JS_NewInt32(ctx, id(j,j->doc->root));
    else if (w_eq(op,"body")) r = JS_NewInt32(ctx, id(j,j->doc->body));
    else if (w_eq(op,"head")) r = JS_NewInt32(ctx, id(j,j->doc->head));
    else if (w_eq(op,"url")) r = JS_NewString(ctx, j->doc->url);
    else if (w_eq(op,"work")) {   /* after a tick: anything left, and when it is due */
        uint32_t at=0; if(argc>2) JS_ToUint32(ctx,&at,argv[2]);
        j->active=argc>1 ? JS_ToBool(ctx,argv[1]) : 0; j->wake_at=at;
    } else if (w_eq(op,"wake")) {   /* a new timer or frame */
        uint32_t at=0; if(argc>2) JS_ToUint32(ctx,&at,argv[2]);
        if(!j->active || (int)(at-j->wake_at)<0) j->wake_at=at;
        j->active=1;
    }
    else if (w_eq(op,"now")) r = JS_NewUint32(ctx, j->now);
    else if (w_eq(op,"title")) {
        if (key) { w_copy(j->doc->title,key,sizeof(j->doc->title)); j->dirty = 1; }
        r = JS_NewString(ctx,j->doc->title);
    } else if (w_eq(op,"log")) { if (key) web_progress(key); }
    else if (w_eq(op,"create") && key) {
        writable(j); WNode *p = web_alloc(sizeof(*p)); p->type = w_eq(key,"#text") ? WN_TEXT : WN_ELEMENT;
        p->name = web_strdup(key,-1); p->tag = w_tag_of(key); r = JS_NewInt32(ctx,id(j,p));
    } else if (w_eq(op,"fetch") && key) {
        char url[512]; w_resolve_url(j->doc->base,key,url,sizeof(url));
        int local = j->doc->url[0]=='/' || w_starts_ci(j->doc->url,"file:");
        if ((!local && !same_origin(j->doc->url,url)) ||
            (!local && !w_starts_ci(url,"https://") && !w_starts_ci(url,"http://")))
            r = JS_ThrowTypeError(ctx,"Cross-origin fetch is not supported");
        else {
            /* The body is copied into a JS string: its arena memory goes at once,
               so the layout below it stays. */
            WebResource res; unsigned int mark=web_mark();
            if (web_fetch(url,&res,WEB_RESOURCE_MAX,0,0) || res.error[0]) r = JS_ThrowTypeError(ctx,"Fetch failed: %s",res.error);
            else {
                r = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx,r,"status",JS_NewInt32(ctx,res.status));
                JS_SetPropertyStr(ctx,r,"url",JS_NewString(ctx,res.url));
                JS_SetPropertyStr(ctx,r,"body",JS_NewStringLen(ctx,res.data,res.length));
            }
            web_release(mark);
        }
    } else if (n && w_eq(op,"get") && key) {
        if (w_eq(key,"nodeName")) r = JS_NewString(ctx,n->name ? n->name : "");
        else if (w_eq(key,"nodeType")) r = JS_NewInt32(ctx,n->type);
        else if (w_eq(key,"parentNode")) r = JS_NewInt32(ctx,id(j,n->parent));
        else if (w_eq(key,"firstChild")) r = JS_NewInt32(ctx,id(j,real(n->first)));
        else if (w_eq(key,"nextSibling")) r = JS_NewInt32(ctx,id(j,real(n->next)));
        else if (w_eq(key,"lastChild")) { WNode *c=n->last; while(c && c->pseudo) c=c->prev; r = JS_NewInt32(ctx,id(j,c)); }
        else if (w_eq(key,"previousSibling")) { WNode *c=n->prev; while(c && c->pseudo) c=c->prev; r = JS_NewInt32(ctx,id(j,c)); }
        else if (w_eq(key,"clientWidth")) r = JS_NewInt32(ctx,n->w);
        else if (w_eq(key,"clientHeight")) r = JS_NewInt32(ctx,n->h);
        else if (w_eq(key,"textContent")) {
            char *buf = malloc(WEB_RESOURCE_MAX + 1);
            if (!buf) r = JS_ThrowOutOfMemory(ctx);
            else { web_text_of(n,buf,WEB_RESOURCE_MAX+1); r = JS_NewString(ctx,buf); free(buf); }
        } else if (w_eq(key,"value") && n->control) r = JS_NewString(ctx,n->control->value);
        else { const char *s = w_attr(n,key); r = s ? JS_NewString(ctx,s) : JS_NULL; }
    } else if (n && w_eq(op,"set") && key && argc>3) {
        const char *v = JS_ToCString(ctx,argv[3]);
        if (v) {
            writable(j);
            if (w_eq(key,"textContent")) {
                if(n->first && n->first==n->last && n->first->type==WN_TEXT && n->first->length>=w_len(v)) {
                    WNode *t=n->first; w_copy(t->text,v,t->length+1);t->length=w_len(v);
                } else {
                while(n->first) unlink_node(n->first);
                if (n->type==WN_TEXT) { n->text=web_strdup(v,-1); n->length=w_len(v); }
                else { WNode *t=web_alloc(sizeof(*t)); t->type=WN_TEXT; t->name="#text"; t->text=web_strdup(v,-1); t->length=w_len(v); append_node(n,t); }
                }
            } else if (w_eq(key,"innerHTML")) {
                WDoc *fragment = web_parse(v,w_len(v),j->doc->url);
                while(n->first) unlink_node(n->first);
                if (fragment && fragment->body) while(fragment->body->first) append_node(n,fragment->body->first);
            } else if (w_eq(key,"value") && n->control) w_copy(n->control->value,v,n->control->capacity);
            else attr(n,key,v);
            j->dirty=1; JS_FreeCString(ctx,v);
        } else r=JS_EXCEPTION;
    } else if (n && w_eq(op,"append") && argc>2) {
        WNode *child=node(j,argv[2]); int cycle=0;
        for(WNode *p=n;p;p=p->parent) if(p==child) cycle=1;
        if(!child || cycle || child==j->doc->root) r=JS_ThrowTypeError(ctx,"Invalid DOM hierarchy");
        else {
            WNode *before=argc>3 ? node(j,argv[3]) : 0;
            writable(j);
            if(before && before->parent==n && before!=child) {
                unlink_node(child); child->parent=n; child->next=before; child->prev=before->prev;
                if(before->prev) before->prev->next=child; else n->first=child;
                before->prev=child;
            } else append_node(n,child);
            j->dirty=1; r=JS_DupValue(ctx,argv[2]);
        }
    } else if (n && w_eq(op,"remove")) { writable(j); unlink_node(n); j->dirty=1; }
    else if (w_eq(op,"query") && key) {   /* querySelector(All) below a node or the document */
        WNode *scope=n ? n : j->doc->root; int first=argc>3 && JS_ToBool(ctx,argv[3]);
        int max=first ? 1 : 65536; WNode **found=malloc(sizeof(*found)*(unsigned)max);
        int count=found ? web_query(j->doc,scope,key,found,max) : 0;
        if(count<0) count=0;   /* selectors the engine cannot read match nothing */
        r=first ? JS_NewInt32(ctx,count ? id(j,found[0]) : 0) : node_array(j,found,count);
        free(found);
    } else if (w_eq(op,"byid") && key) {
        WNode *f=0;
        for(WNode *m=j->doc->root;m && !f;) {
            if(m->type==WN_ELEMENT && m->id && !m->pseudo && w_eq(m->id,key)) f=m;
            if(m->first && !m->pseudo) { m=m->first; continue; }
            while(m && !m->next) m=m->parent;
            if(m) m=m->next;
        }
        r=JS_NewInt32(ctx,id(j,f));
    } else if (n && w_eq(op,"children")) {   /* elements, or all nodes with a true 4th argument */
        int all=argc>3 && JS_ToBool(ctx,argv[3]), k=0; r=JS_NewArray(ctx);
        for(WNode *c=real(n->first);c;c=real(c->next)) if(all || c->type==WN_ELEMENT) JS_SetPropertyUint32(ctx,r,(uint32_t)k++,JS_NewInt32(ctx,id(j,c)));
    }
    else if (n && w_eq(op,"canvas") && key) {
        if(w_eq(key,"init") || w_eq(key,"resize")) writable(j);   /* the element's size */
        r=web_canvas_call(j,n,key,argc-3,argv+3);
    }
    JS_FreeCString(ctx,key); JS_FreeCString(ctx,op); return r;
}
static JSModuleDef *module_loader(JSContext *ctx, const char *name, void *opaque) {
    WJs *j=opaque; WebResource r;
    if (!same_origin(j->doc->url,name) && !w_starts_ci(j->doc->url,"file:") && j->doc->url[0]!='/') {
        JS_ThrowTypeError(ctx,"Cross-origin modules are not supported"); return 0;
    }
    writable(j);
    if(web_fetch(name,&r,WEB_RESOURCE_MAX,0,0) || r.error[0]) { JS_ThrowReferenceError(ctx,"Cannot load module %s",name); return 0; }
    JSValue v=JS_Eval(ctx,r.data,r.length,name,JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY);
    if(JS_IsException(v)) return 0;
    JSModuleDef *m=JS_VALUE_GET_PTR(v); JS_FreeValue(ctx,v); return m;
}
static char *module_normalize(JSContext *ctx, const char *base, const char *name, void *opaque) {
    (void)opaque; char url[512];
    if (name[0]!='.' && name[0]!='/' && !strstr(name,"://")) { JS_ThrowReferenceError(ctx,"Bare module specifier: %s",name); return 0; }
    w_resolve_url(base,name,url,sizeof(url)); return js_strdup(ctx,url);
}
static void run(WJs *j,const char *src,int length,const char *name,int flags) {
    JSValue v=JS_Eval(j->ctx,src,length,name,flags);
    if(JS_IsException(v)) exception(j);
    JS_FreeValue(j->ctx,v);
}
static void jobs(WJs *j) {
    JSContext *ctx; int count=0,r;
    while(!j->stopped && count++<256 && (r=JS_ExecutePendingJob(j->runtime,&ctx))!=0) if(r<0) { exception(j); break; }
}
void web_js_destroy(WDoc *d) {
    if(!d || !d->js) return;
    WJs *j=d->js; web_canvas_free(j); JS_FreeContext(j->ctx); JS_FreeRuntime(j->runtime); free(j->nodes); free(j); d->js=0;
}
void web_js_load(WDoc *d) {
    WJs *j=calloc(1,sizeof(*j)); if(!j) return;
    j->doc=d; j->budget=20000; j->draw_budget=16000000; j->unlaid=1; j->now=web_time(); d->js=j;
    j->runtime=JS_NewRuntime(); if(!j->runtime) { free(j); d->js=0; return; }
    JS_SetMemoryLimit(j->runtime,32u<<20); JS_SetMaxStackSize(j->runtime,512u<<10);
    JS_SetStripInfo(j->runtime,JS_STRIP_SOURCE);   /* Function.prototype.toString gives no source: less memory */
    JS_SetInterruptHandler(j->runtime,interrupted,j);
    JS_SetModuleLoaderFunc(j->runtime,module_normalize,module_loader,j);
    j->ctx=JS_NewContext(j->runtime); if(!j->ctx) { JS_FreeRuntime(j->runtime); free(j); d->js=0; return; }
    JS_SetContextOpaque(j->ctx,j);
    JSValue global=JS_GetGlobalObject(j->ctx);
    JS_SetPropertyStr(j->ctx,global,"__bridge",JS_NewCFunction(j->ctx,bridge,"__bridge",1)); JS_FreeValue(j->ctx,global);
    run(j,(const char *)web_bootstrap,sizeof(web_bootstrap)-1,"barnix:dom",JS_EVAL_TYPE_GLOBAL);
    /* Snapshot scripts: DOM changes cannot invalidate the traversal or execute inserted scripts twice. */
    WNode *scripts[256]; int count=0;
    for(WNode *n=d->root;n && count<256;) {
        if(n->tag==T_SCRIPT) scripts[count++]=n;
        if(n->first && n->tag!=T_SCRIPT) { n=n->first; continue; }
        while(n && !n->next) n=n->parent;
        if(n) n=n->next;
    }
    for(int i=0;i<count && !j->stopped && !web_out_of_memory;i++) {
        WNode *n=scripts[i]; const char *type=w_attr(n,"type"), *src=w_attr(n,"src");
        int module=type && w_eq(type,"module");
        if(type && type[0] && !module && !w_eq_ci(type,"text/javascript") && !w_eq_ci(type,"application/javascript")) continue;
        if(!module && w_attr(n,"nomodule")) continue;
        char url[512]; w_copy(url,d->url,sizeof(url));
        const char *code=n->first ? n->first->text : ""; int length=n->first ? n->first->length : 0;
        WebResource r;
        if(src) {
            w_resolve_url(d->base,src,url,sizeof(url));
            if((w_starts_ci(d->url,"http:") || w_starts_ci(d->url,"https:")) && !w_starts_ci(url,"http:") && !w_starts_ci(url,"https:")) continue;
            if(web_fetch(url,&r,WEB_RESOURCE_MAX,0,0) || r.error[0]) { w_copy(j->error,r.error,sizeof(j->error)); continue; }
            code=r.data; length=r.length;
            if(!w_valid_utf8(code,length)) {   /* a KOI8-R or windows-1251 site */
                int kind=web_charset_of(r.type,w_len(r.type));
                code=web_convert(code,length,kind ? kind : d->charset ? d->charset : WCS_1251,&length);
            }
        }
        run(j,code,length,url,module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL); jobs(j);
    }
    run(j,"__webLoaded()",13,"barnix:load",JS_EVAL_TYPE_GLOBAL); jobs(j);
    d->mark=web_mark();
}
/* One tick or event may hold the desktop this long (ms); a page that keeps
 * overrunning has its scripts stopped. */
#define SLOW_SLICE 60
#define SLOW_STRIKES 40
/* What the scripts did: the layout must be redone (the DOM changed; its
 * memory now holds the new nodes) or only repainted (canvas, title). */
static int finish(WJs *j) {
    j->deadline=0;
    if(!j->overran) j->strikes=0;
    else if(++j->strikes>=SLOW_STRIKES) { j->stopped=1; w_copy(j->error,"the page's scripts are too slow: stopped",sizeof(j->error)); }
    else w_copy(j->error,"a script took too long and was cut short",sizeof(j->error));
    j->changes=j->unlaid ? 2 : j->dirty ? 1 : 0;
    if(j->unlaid) j->doc->mark=web_mark();
    return j->changes;
}
static void begin(WJs *j) {
    j->budget=1500; j->draw_budget=16000000; j->dirty=0; j->unlaid=0; j->changes=0;
    j->overran=0; j->deadline=web_time()+SLOW_SLICE; if(!j->deadline) j->deadline=1;
    JS_UpdateStackTop(j->runtime);
}
int web_js_tick(WDoc *d,unsigned int now,int width,int height) {
    if(!d || !d->js) return 0;
    WJs *j=d->js;
    if(j->stopped) return 0;
    /* Nothing due: no call into JavaScript, and the layout stays. */
    if(!JS_IsJobPending(j->runtime) && d->width==width && d->viewport_h==height && (!j->active || (int)(now-j->wake_at)<0)) return 0;
    begin(j); j->now=now;
    JSValue g=JS_GetGlobalObject(j->ctx), f=JS_GetPropertyStr(j->ctx,g,"__webTick");
    JSValue args[]={JS_NewUint32(j->ctx,now),JS_NewInt32(j->ctx,width),JS_NewInt32(j->ctx,height)};
    JSValue v=JS_Call(j->ctx,f,g,3,args);
    if(JS_IsException(v)) exception(j);
    JS_FreeValue(j->ctx,v); JS_FreeValue(j->ctx,f); JS_FreeValue(j->ctx,g); jobs(j);
    return finish(j);
}
int web_js_event(WDoc *d,WNode *target,const char *type,int x,int y,int key) {
    if(!d || !d->js) return 0;
    WJs *j=d->js;
    if(j->stopped) return 0;
    begin(j);
    JSValue g=JS_GetGlobalObject(j->ctx), f=JS_GetPropertyStr(j->ctx,g,"__webEvent");
    JSValue args[]={JS_NewInt32(j->ctx,id(j,target ? target : d->body)),JS_NewString(j->ctx,type),JS_NewInt32(j->ctx,x),JS_NewInt32(j->ctx,y),JS_NewInt32(j->ctx,key)};
    JSValue v=JS_Call(j->ctx,f,g,5,args); int handled=0;
    if(JS_IsException(v)) exception(j); else handled=JS_ToBool(j->ctx,v);
    for(int i=0;i<5;i++) JS_FreeValue(j->ctx,args[i]);
    JS_FreeValue(j->ctx,v); JS_FreeValue(j->ctx,f); JS_FreeValue(j->ctx,g); jobs(j);
    finish(j);
    return handled;
}
int web_js_changes(WDoc *d) { return d && d->js ? ((WJs *)d->js)->changes : 0; }
const char *web_js_error(WDoc *d) { return d && d->js ? ((WJs *)d->js)->error : ""; }
