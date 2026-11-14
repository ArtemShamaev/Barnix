/* Barnocraft full screen. The game core is barnocraft_game.c; graphics,
 * keys with releases and relative mouse motion come from the BDK Barnix
 * Graphics library (BDK/include/barnix_gfx.h). */
#include "command.h"
#include "barnocraft_game.h"

#define MAX_W 1024
#define MAX_H 768
/* Without extended memory the game falls back to a 640x400 canvas. */
static unsigned int small_screen[640 * 400], small_view[640 * 400];

int main(int argc, const char *const *argv)
{
    (void)argv;
    if (argc > 1) return fail("barnocraft - voxel sandbox; no arguments");
    barnix->clear();
    barnix->puts("Barnocraft: generating world...");
    bc_init();
    int width = 640, height = 400;
    unsigned int *pixels = small_screen, *view = small_view;
    if (!bgx_display(&width, &height)) {
        if (width > MAX_W) width = MAX_W;
        if (height > MAX_H) height = MAX_H;
        if (width < 640 || height < 400) { width = 640; height = 400; }
        unsigned int bytes = (unsigned int)width * height * 8;
        unsigned int *memory = width * height > 640 * 400 ? barnix->memory(&bytes) : 0;
        if (memory && bytes >= (unsigned int)width * height * 8) { pixels = memory; view = memory + width * height; }
        else { width = 640; height = 400; }
    } else { width = 640; height = 400; }
    BgxCanvas screen;
    if (bgx_open(&screen, pixels, width, height))
        return fail("barnocraft: this display has no graphics mode");

    static unsigned char held[256], pressed[256], typed[256];
    BcInput input = {held, pressed, typed, width / 2, height / 2, 0, 0, 0, 0, 0, 1, 0};
    unsigned int last = bgx_millis();
    for (;;) {
        unsigned int now = bgx_millis();
        int dt = (int)(now - last);
        last = now;
        for (int i = 0; i < 256; i++) pressed[i] = typed[i] = 0;
        BgxEvent event;
        while (bgx_poll(&event))
            if (event.type == BGX_PRESS) { typed[event.code] = 1; if (!event.repeat) pressed[event.code] = 1; }
        for (int i = 0; i < 256; i++) held[i] = bgx_key_held(i);
        BgxMouse mouse;
        bgx_mouse(&mouse);
        input.dx = mouse.dx; input.dy = mouse.dy; input.wheel = mouse.wheel; input.clicked = mouse.pressed;
        input.moved = mouse.dx || mouse.dy;
        input.mouse_x += mouse.dx; input.mouse_y += mouse.dy;
        if (input.mouse_x < 0) input.mouse_x = 0;
        if (input.mouse_x >= width) input.mouse_x = width - 1;
        if (input.mouse_y < 0) input.mouse_y = 0;
        if (input.mouse_y >= height) input.mouse_y = height - 1;
        if (bc_frame(&screen, view, &input, dt) & BC_QUIT) break;
        bgx_present(&screen);
    }
    bgx_close();
    barnix->clear();
    return 0;
}
