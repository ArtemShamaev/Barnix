#ifndef BARNIX_OPENGL_H
#define BARNIX_OPENGL_H
#include <barnix_gfx.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix OpenGL context (BXGL), ABI 11. OpenGL 1.1 in software: geometry
 * on the calling core, triangles rasterized in bands on every core.
 *
 *   if (bxgl_open(0, 0)) return 1;          // full screen, display size
 *   for (;;) {
 *       ...bgx_poll / bgx_mouse for input...
 *       glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
 *       ...GL calls...
 *       bxgl_swap();
 *   }
 *   bxgl_close();
 *
 * The context takes the program's extended memory (barnix->memory): use
 * bxgl_alloc instead of calling barnix->memory yourself. Without extended
 * memory a 320x200 context still works from a small built-in arena. */

/* width/height 0 pick the display size (at most 1024x768). Returns 0, or -1
 * without a graphics display or memory for the buffers. */
int bxgl_open(int width, int height);
int bxgl_width(void);
int bxgl_height(void);
/* Finish the frame and show it. */
int bxgl_swap(void);
void bxgl_close(void);
/* The color buffer as a BGX canvas, for 2D text and images over the 3D
 * frame: call glFinish() first, draw with bgx_*, then bxgl_swap(). */
BgxCanvas *bxgl_canvas(void);

/* Off-screen context: render into caller-owned memory. color is
 * width*height 0x00RRGGBB values (top row first), depth width*height floats,
 * heap at least 256 KiB for triangles, textures and display lists. */
int bxgl_context(unsigned int *color, float *depth, int width, int height, void *heap, unsigned int heap_bytes);

/* Memory from the context heap (16-byte aligned, zeroed). */
void *bxgl_alloc(unsigned int bytes);
void bxgl_free(void *block);
unsigned int bxgl_free_memory(void);

/* Frames per second over the last second, measured by bxgl_swap. */
int bxgl_fps(void);
#ifdef __cplusplus
}
#endif
#endif
