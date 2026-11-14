/* Barnocraft in a window. Click the view to play (the mouse is captured),
 * Esc pauses and frees the mouse, F11 switches to full screen. In the
 * background the world keeps running at a third of the frame rate. */
#include "gouse.h"
#include "../barnocraft_game.h"

typedef struct { int dt, clicked, wheel, release, mx, my, moved; } State;
#define S ((State *)w->state)
static unsigned int *view;
static int world_ready;
static const unsigned char no_keys[256];

static void open(GWindow *w, const char *argument)
{
    (void)argument;
    if (!view) view = g_alloc((unsigned int)g_screen_w * g_screen_h * 4);
    if (!view) { g_error(w, "Barnocraft", "Not enough memory for the 3D view."); return; }
    if (!world_ready) { bc_init(); world_ready = 1; }
    S->mx = w->w / 2; S->my = w->h / 2;
}
static void tick(GWindow *w, int dt) { S->dt += dt; }
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    if (e->type == EV_MOUSE_DOWN) s->clicked |= e->button == 1 ? 1 : e->button == 2 ? 2 : 4;
    else if (e->type == EV_WHEEL) s->wheel += e->id;
    else if (e->type == EV_BLUR) s->release = 1;
    if (e->type == EV_MOUSE_DOWN || e->type == EV_MOUSE_MOVE) {
        if (e->x != s->mx || e->y != s->my) s->moved = 1;
        s->mx = e->x; s->my = e->y;
    }
}
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    if (!view) { bgx_fill(c, 0, 0, w->w, w->h, 0); return; }
    int focused = g_focused(w), captured = g_captured(w);
    BcInput input;
    input.held = focused ? g_held : no_keys;
    input.pressed = focused ? g_pressed : no_keys;
    input.typed = focused ? g_typed : no_keys;
    input.mouse_x = s->mx; input.mouse_y = s->my; input.moved = s->moved;
    input.dx = captured ? g_look_dx : 0; input.dy = captured ? g_look_dy : 0;
    input.wheel = s->wheel; input.clicked = s->clicked;
    input.draw_pointer = 0; input.release = s->release;
    int result = bc_frame(c, view, &input, s->dt);
    s->dt = 0; s->clicked = 0; s->wheel = 0; s->release = 0; s->moved = 0;
    if ((result & BC_CAPTURE) && focused && !captured) g_capture(w, 1);
    if (!(result & BC_CAPTURE) && captured) g_capture(w, 0);
    if (result & BC_QUIT) g_close(w);
}
const GApp app_barnocraft = {"Barnocraft", "Build and dig in a voxel world", ICON_BARNOCRAFT, 600, 380, 440, 300,
                             APP_RESIZABLE | APP_CONTINUOUS | APP_SINGLE | APP_FULLSCREEN, 0, 0, sizeof(State),
                             open, event, tick, paint, 0};
