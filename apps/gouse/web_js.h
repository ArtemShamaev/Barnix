#ifndef WEB_JS_H
#define WEB_JS_H
#include "web.h"
/* QuickJS is vendored code; its inline helpers trip -Wextra, ours must not. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "vendor/quickjs/quickjs.h"
#pragma GCC diagnostic pop
#define WJS_NODES 262144
#define WJS_CANVASES 32
typedef struct WJs WJs;
typedef struct WCanvasState WCanvasState;
struct WJs {
    JSRuntime *runtime;
    JSContext *ctx;
    WDoc *doc;
    WNode **nodes;                  /* handle -> node; WNode.js_id is the way back */
    int count, capacity, budget, dirty, stopped, active;
    int unlaid;                     /* the layout was dropped: the DOM changed */
    int changes;                    /* of the last tick or event: 0 none, 1 repaint, 2 relayout */
    unsigned int wake_at;           /* the next timer or frame is due then */
    unsigned int deadline;          /* web_time() when a tick or an event is cut short, 0 = none */
    int overran, strikes;           /* cut short: this time, and how many times in a row */
    unsigned int canvas_bytes, draw_budget;
    unsigned int now;
    char error[256];
    WCanvasState *canvases[WJS_CANVASES];
    int canvas_count;
};
JSValue web_canvas_call(WJs *js, WNode *node, const char *op, int argc, JSValueConst *argv);
void web_canvas_free(WJs *js);
#endif
