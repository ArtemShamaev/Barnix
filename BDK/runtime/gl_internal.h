#ifndef BXGL_INTERNAL_H
#define BXGL_INTERNAL_H
/* Barnix OpenGL internals shared by gl.c (state, geometry, lists, textures)
 * and gl_raster.c (triangle setup, band rasterizer, clears). */
#include <barnix_opengl.h>

#define GL_TEX_LEVELS 13
typedef struct { unsigned int *texels; int w, h, wmask, hmask; } GlLevel;   /* mask -1: not a power of two */
typedef struct GlTexture {
    GlLevel level[GL_TEX_LEVELS];
    int min_filter, mag_filter, wrap_s, wrap_t, alpha, luminance_only, generate_mipmap, max_level;
} GlTexture;

/* Everything a fragment needs, snapshotted per batch of triangles. */
#define RF_DEPTH_TEST 1U
#define RF_DEPTH_WRITE 2U
#define RF_BLEND 4U
#define RF_ALPHA_TEST 8U
#define RF_TEXTURE 16U
#define RF_FOG 32U
#define RF_OFFSET 64U
#define RF_TEXTURE1 128U
#define RF_QUERY 256U
#define GL_TEXTURE_UNITS 2
typedef struct {
    const GlTexture *tex;
    int min_filter, mag_filter, wrap_s, wrap_t, env, tex_alpha, levels;
    int env_color[4];
} GlUnit;
typedef struct {
    unsigned int flags, color_mask;
    int depth_func, alpha_func, alpha_ref, blend_src, blend_dst, blend_eq;
    int x0, y0, x1, y1;                      /* pixel rectangle, canvas coordinates */
    int fog_color[3], blend_color[4];
    unsigned int query;                      /* occlusion query counting samples, 0 = none */
    float offset_factor, offset_units;
    GlUnit unit[GL_TEXTURE_UNITS];
} GlRaster;
typedef struct { float x, y, z, iw, r, g, b, a, s, t, f, s1, t1; } GlScreenVertex;

enum { PZ, PR, PG, PB, PA, PF, PW, PS, PT, PS1, PT1, PLANES };
typedef struct {
    double ea[3], eb[3], ec[3];              /* edge functions, exact on a 1/16 pixel grid */
    float p[PLANES][3];                      /* value at (x0, y0), d/dx, d/dy */
    float x0, y0;
    short minx, miny, maxx, maxy;
    unsigned short state;
    unsigned char tie, level, linear, level1, linear1;
} GlTri;

typedef struct {
    unsigned int *color;
    float *depth;
    int width, height;
    GlTri *tris;
    unsigned int tri_count, tri_cap;
    GlRaster *states;
    unsigned int state_count, state_cap;
    unsigned int *query_counts;              /* [band][state] samples that passed */
    unsigned int query_bands;
    void (*query_add)(unsigned int query, unsigned int samples);
} GlFrame;
extern GlFrame gl_frame;

void gl_raster_triangle(const GlScreenVertex *a, const GlScreenVertex *b, const GlScreenVertex *c, const GlRaster *state);
void gl_raster_flush(void);
void gl_raster_clear(const GlRaster *rect, int color, unsigned int value, int depth, float depth_value);
void gl_fpu_enable(void);

static inline int gl_iround(double v)
{
    int i;
    __asm__("fistpl %0" : "=m"(i) : "t"(v) : "st");
    return i;
}
static inline int gl_ifloor(double v) { int i = gl_iround(v); return i > v ? i - 1 : i; }
#endif
