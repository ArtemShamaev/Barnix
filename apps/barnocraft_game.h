#ifndef BARNOCRAFT_GAME_H
#define BARNOCRAFT_GAME_H
#include <barnix_gfx.h>
/* Input for one frame. Key arrays are indexed by BGX_KEY_* codes. */
typedef struct {
    const unsigned char *held, *pressed, *typed;  /* typed: presses + auto-repeat */
    int mouse_x, mouse_y, moved;                  /* pointer inside the canvas */
    int dx, dy, wheel;                            /* look: relative motion while captured */
    unsigned int clicked;                         /* buttons pressed this frame */
    int draw_pointer, release;                    /* release: host took the mouse back */
} BcInput;
#define BC_QUIT 1
#define BC_CAPTURE 2   /* playing: the host should hide and capture the mouse */
void bc_init(void);
/* view_buffer holds at least canvas->width * canvas->height pixels. */
int bc_frame(BgxCanvas *canvas, unsigned int *view_buffer, const BcInput *input, int dt);
#endif
