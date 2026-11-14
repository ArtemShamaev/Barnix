#include "gl_internal.h"
/* Barnix OpenGL: state, transform, lighting, clipping, primitive assembly,
 * textures, vertex arrays and display lists. Rasterization is gl_raster.c. */

/* ================================================================ heap */
typedef struct { unsigned int size, used, pad[2]; } Block;   /* 16 bytes, size excludes the header */
static unsigned char *heap_base, *heap_rover;
static unsigned int heap_size;

static void zero(void *p, unsigned int bytes)
{
    unsigned int *w = p;
    for (unsigned int i = 0; i < bytes / 4; i++) w[i] = 0;
}
static void heap_init(void *memory, unsigned int bytes)
{
    unsigned int skew = (16 - ((unsigned long)memory & 15)) & 15;
    heap_base = (unsigned char *)memory + skew;
    heap_size = bytes > skew + 64 ? (bytes - skew) & ~15U : 0;
    if (heap_size) { Block *b = (Block *)heap_base; b->size = heap_size - 16; b->used = 0; }
    heap_rover = heap_base;
}
/* Next fit: the search resumes where the last allocation ended. */
void *bxgl_alloc(unsigned int bytes)
{
    bytes = bytes ? (bytes + 15) & ~15U : 16;
    unsigned char *end = heap_base + heap_size;
    for (int pass = 0; pass < 2; pass++) {
        unsigned char *p = pass ? heap_base : heap_rover, *stop = pass ? heap_rover : end;
        if (!p) return 0;
        while (p < stop) {
        Block *b = (Block *)p;
        if (!b->used) {
            for (;;) {   /* coalesce the free blocks that follow */
                if (p + 16 + b->size == heap_rover) heap_rover = p;
                unsigned char *q = p + 16 + b->size;
                if (q >= end || ((Block *)q)->used) break;
                b->size += 16 + ((Block *)q)->size;
            }
            if (b->size >= bytes) {
                if (b->size >= bytes + 64) {
                    Block *rest = (Block *)(p + 16 + bytes);
                    rest->size = b->size - bytes - 16; rest->used = 0;
                    b->size = bytes;
                }
                b->used = 1;
                zero(p + 16, b->size);
                heap_rover = p + 16 + b->size;
                if (heap_rover >= end) heap_rover = heap_base;
                return p + 16;
            }
        }
        p += 16 + b->size;
        }
    }
    return 0;
}
void bxgl_free(void *block) { if (block) ((Block *)block - 1)->used = 0; }
/* Resizes a block where it lies: shrinking splits off the tail, growing
 * takes the free blocks that follow. Returns 0 when it cannot grow. */
static int heap_resize(void *block, unsigned int bytes)
{
    Block *b = (Block *)block - 1;
    unsigned char *p = (unsigned char *)b, *end = heap_base + heap_size;
    bytes = bytes ? (bytes + 15) & ~15U : 16;
    unsigned int old = b->size;
    while (b->size < bytes) {
        unsigned char *q = p + 16 + b->size;
        if (q >= end || ((Block *)q)->used) break;
        if (q == heap_rover) heap_rover = p;
        b->size += 16 + ((Block *)q)->size;
    }
    if (b->size < bytes) return 0;
    if (b->size > old) zero(p + 16 + old, (bytes < b->size ? bytes : b->size) - old);
    if (b->size >= bytes + 64) {
        Block *rest = (Block *)(p + 16 + bytes);
        rest->size = b->size - bytes - 16; rest->used = 0;
        b->size = bytes;
    }
    return 1;
}
unsigned int bxgl_free_memory(void)
{
    unsigned int total = 0;
    for (unsigned char *p = heap_base; p && p < heap_base + heap_size; p += 16 + ((Block *)p)->size)
        if (!((Block *)p)->used) total += ((Block *)p)->size;
    return total;
}

/* ================================================================ state */
#define E_DEPTH 1U
#define E_CULL 2U
#define E_TEXTURE 4U
#define E_BLEND 8U
#define E_LIGHTING 16U
#define E_FOG 32U
#define E_ALPHA 64U
#define E_COLOR_MATERIAL 128U
#define E_NORMALIZE 256U
#define E_SCISSOR 512U
#define E_OFFSET 1024U
#define E_TEXTURE1 2048U
#define E_LIGHT0 65536U
#define MAX_TEXTURES 1024
#define MAX_LISTS (1U << 21)
#define LIST_PAGE 1024U
#define MAX_LIST_RANGES 512
#define MAX_BUFFERS 4096
#define MAX_QUERIES 8192
#define UNITS GL_TEXTURE_UNITS

typedef struct {
    float ambient[4], diffuse[4], specular[4], position[4], spot[3], spot_exponent, spot_cutoff, attenuation[3];
    float dir[3], spot_unit[3], spot_cos;   /* derived */
} GlLight;
typedef struct { float ambient[4], diffuse[4], specular[4], emission[4], shininess; } GlMaterial;
typedef struct {
    unsigned int enables;
    float color[4], normal[3], tex[UNITS][4];
    float clear_color[4], clear_depth;
    int depth_func, depth_mask, blend_src, blend_dst, alpha_func;
    float alpha_ref;
    int cull_face, front_face, poly_mode[2], shade_model;
    float point_size, line_width, offset_factor, offset_units;
    int scissor[4], viewport[4];
    float depth_near, depth_far;
    int color_mask[4];
    GlLight light[8];
    GlMaterial material[2];
    float model_ambient[4];
    int two_side, local_viewer, cm_face, cm_mode;
    int fog_mode;
    float fog_density, fog_start, fog_end, fog_color[4];
    int env_mode[UNITS];
    float env_color[UNITS][4];
    unsigned int bound[UNITS];
    int active_unit;
    int matrix_mode;
    int blend_eq;
    float blend_color[4];
    float secondary[3], fog_coord, window_pos[3];
} GlState;

typedef struct { float clip[4], col[4], s[UNITS], t[UNITS], fog; unsigned int out; } GlVertex;
/* pointer is an offset into buffer when buffer is not 0 (OpenGL 1.5). */
typedef struct { int size, type, stride, enabled; const unsigned char *pointer; unsigned int buffer; } GlArray;
typedef struct { unsigned char *data; unsigned int size, usage; } GlBuffer;
typedef struct { unsigned int result, active, used; } GlQuery;
typedef struct { float *words; unsigned int count, cap; } GlList;

static GlState S;
static GlState attrib_stack[8];
static int attrib_depth;
static GLenum error_flag;
static int ready;
static BgxCanvas canvas;

static float mv_stack[32][16], proj_stack[8][16], tex_stack[UNITS][8][16];
static int mv_top, proj_top, tex_top[UNITS], matrices_dirty = 1, tex_identity[UNITS] = {1, 1};
static float normal_m[9];

static GlTexture default_texture, *textures[MAX_TEXTURES];
static unsigned char texture_names[MAX_TEXTURES];
/* Display lists live in pages of LIST_PAGE pointers taken from the heap
 * when a list is first compiled there, so programs may reserve far more
 * names than they fill. Reserved names are kept as ranges. */
static GlList **list_pages[MAX_LISTS / LIST_PAGE];
static struct { unsigned int base, count; } list_ranges[MAX_LIST_RANGES];
static int list_range_count;
static GlList *list_get(unsigned int name)
{
    if (!name || name >= MAX_LISTS || !list_pages[name / LIST_PAGE]) return 0;
    return list_pages[name / LIST_PAGE][name % LIST_PAGE];
}
static GlList *compiling;
static int compile_mode, call_depth;
static unsigned int compiling_name, list_base;

static GlArray va_vertex, va_color, va_normal, va_tex[UNITS];
static int client_unit;
static GlBuffer *buffers[MAX_BUFFERS];
static unsigned char buffer_names[MAX_BUFFERS];
static unsigned int array_buffer, element_buffer;
static GlQuery queries[MAX_QUERIES];
static unsigned int active_query;
static int unpack_alignment = 4, pack_alignment = 4, unpack_row_length;

static int prim_mode = -1, prim_n;
static GlVertex pv[4];
static GlRaster raster_fill, raster_line;

static void set_error(GLenum error) { if (!error_flag) error_flag = error; }
GLenum glGetError(void) { GLenum e = error_flag; error_flag = GL_NO_ERROR; return e; }

static void copy4(float *out, float a, float b, float c, float d) { out[0] = a; out[1] = b; out[2] = c; out[3] = d; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ============================================================= matrices */
static void identity(float *m) { for (int i = 0; i < 16; i++) m[i] = i % 5 ? 0.0f : 1.0f; }
static void copy16(float *out, const float *in) { for (int i = 0; i < 16; i++) out[i] = in[i]; }
static void mul(float *out, const float *a, const float *b)   /* out = a * b, column major */
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] + a[12 + row] * b[c * 4 + 3];
    copy16(out, r);
}
static void transform(float *out, const float *m, const float *v)
{
    float x = v[0], y = v[1], z = v[2], w = v[3];
    for (int i = 0; i < 4; i++) out[i] = m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i] * w;
}
static float *current_matrix(void)
{
    if (S.matrix_mode == GL_PROJECTION) return proj_stack[proj_top];
    if (S.matrix_mode == GL_TEXTURE) return tex_stack[S.active_unit][tex_top[S.active_unit]];
    return mv_stack[mv_top];
}
static void matrix_changed(void)
{
    matrices_dirty = 1;
    if (S.matrix_mode == GL_TEXTURE) {
        int u = S.active_unit;
        const float *t = tex_stack[u][tex_top[u]];
        tex_identity[u] = 1;
        for (int i = 0; i < 16; i++) if (t[i] != (i % 5 ? 0.0f : 1.0f)) tex_identity[u] = 0;
    }
}
static void update_matrices(void)
{
    const float *m = mv_stack[mv_top];
    /* Normals use the inverse transpose of the upper 3x3: cofactors / det. */
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float c00 = e * i - f * h, c01 = f * g - d * i, c02 = d * h - e * g;
    float c10 = c * h - b * i, c11 = a * i - c * g, c12 = b * g - a * h;
    float c20 = b * f - c * e, c21 = c * d - a * f, c22 = a * e - b * d;
    float det = a * c00 + b * c01 + c * c02, inv = det != 0 ? 1.0f / det : 1.0f;
    float n[9] = {c00, c01, c02, c10, c11, c12, c20, c21, c22};   /* row r: normal component r */
    for (int k = 0; k < 9; k++) normal_m[k] = n[k] * inv;
    matrices_dirty = 0;
}
static void mult_impl(const float *m)
{
    float *t = current_matrix();
    mul(t, t, m);
    matrix_changed();
}
static void translate_impl(float x, float y, float z)
{
    float m[16]; identity(m); m[12] = x; m[13] = y; m[14] = z; mult_impl(m);
}
static void scale_impl(float x, float y, float z)
{
    float m[16]; identity(m); m[0] = x; m[5] = y; m[10] = z; mult_impl(m);
}
static void rotate_impl(float angle, float x, float y, float z)
{
    float len = sqrtf(x * x + y * y + z * z);
    if (len == 0) return;
    x /= len; y /= len; z /= len;
    float r = angle * (float)(M_PI / 180.0), c = cosf(r), s = sinf(r), k = 1 - c, m[16];
    identity(m);
    m[0] = x * x * k + c;     m[4] = x * y * k - z * s; m[8] = x * z * k + y * s;
    m[1] = y * x * k + z * s; m[5] = y * y * k + c;     m[9] = y * z * k - x * s;
    m[2] = x * z * k - y * s; m[6] = y * z * k + x * s; m[10] = z * z * k + c;
    mult_impl(m);
}
static void ortho_impl(float l, float r, float b, float t, float n, float f)
{
    if (l == r || b == t || n == f) { set_error(GL_INVALID_VALUE); return; }
    float m[16]; identity(m);
    m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n);
    mult_impl(m);
}
static void frustum_impl(float l, float r, float b, float t, float n, float f)
{
    if (n <= 0 || f <= 0 || l == r || b == t || n == f) { set_error(GL_INVALID_VALUE); return; }
    float m[16]; zero(m, sizeof(m));
    m[0] = 2 * n / (r - l); m[5] = 2 * n / (t - b);
    m[8] = (r + l) / (r - l); m[9] = (t + b) / (t - b); m[10] = -(f + n) / (f - n); m[11] = -1;
    m[14] = -2 * f * n / (f - n);
    mult_impl(m);
}
static void push_impl(void)
{
    if (S.matrix_mode == GL_PROJECTION) {
        if (proj_top == 7) { set_error(GL_STACK_OVERFLOW); return; }
        copy16(proj_stack[proj_top + 1], proj_stack[proj_top]); proj_top++;
    } else if (S.matrix_mode == GL_TEXTURE) {
        int u = S.active_unit;
        if (tex_top[u] == 7) { set_error(GL_STACK_OVERFLOW); return; }
        copy16(tex_stack[u][tex_top[u] + 1], tex_stack[u][tex_top[u]]); tex_top[u]++;
    } else {
        if (mv_top == 31) { set_error(GL_STACK_OVERFLOW); return; }
        copy16(mv_stack[mv_top + 1], mv_stack[mv_top]); mv_top++;
    }
}
static void pop_impl(void)
{
    int *top = S.matrix_mode == GL_PROJECTION ? &proj_top : S.matrix_mode == GL_TEXTURE ? &tex_top[S.active_unit] : &mv_top;
    if (!*top) { set_error(GL_STACK_UNDERFLOW); return; }
    (*top)--;
    matrix_changed();
}

/* ============================================================== textures */
static GlTexture *texture_object(unsigned int name)
{
    if (!name) return &default_texture;
    if (name >= MAX_TEXTURES) return 0;
    if (!textures[name]) {
        GlTexture *t = bxgl_alloc(sizeof(GlTexture));
        if (!t) { set_error(GL_OUT_OF_MEMORY); return 0; }
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR; t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
        t->max_level = 1000;
        textures[name] = t;
        texture_names[name] = 1;
    }
    return textures[name];
}
static GlTexture *bound_texture(void) { return texture_object(S.bound[S.active_unit]); }
static int texture_levels(const GlTexture *t)
{
    int n = 0;
    while (n < GL_TEX_LEVELS && n <= t->max_level && t->level[n].texels) {
        int w = t->level[0].w >> n, h = t->level[0].h >> n;
        if (t->level[n].w != (w ? w : 1) || t->level[n].h != (h ? h : 1)) break;
        n++;
        if (w <= 1 && h <= 1) break;
    }
    return n;
}

/* ============================================================ rasterizer */
static int scale255(float v) { return gl_iround(clampf(v, 0, 1) * 255.0f); }
static void build_raster(void)
{
    GlRaster *r = &raster_fill;
    zero(r, sizeof(*r));
    if (S.enables & E_DEPTH) {
        r->flags |= RF_DEPTH_TEST;
        if (S.depth_mask) r->flags |= RF_DEPTH_WRITE;
    }
    r->depth_func = S.depth_func;
    if (S.enables & E_BLEND) {
        r->flags |= RF_BLEND; r->blend_src = S.blend_src; r->blend_dst = S.blend_dst; r->blend_eq = S.blend_eq;
        for (int k = 0; k < 4; k++) r->blend_color[k] = scale255(S.blend_color[k]);
    }
    if (active_query) { r->flags |= RF_QUERY; r->query = active_query; }
    if (S.enables & E_ALPHA) { r->flags |= RF_ALPHA_TEST; r->alpha_func = S.alpha_func; r->alpha_ref = scale255(S.alpha_ref); }
    r->color_mask = (S.color_mask[0] ? 0xff0000U : 0) | (S.color_mask[1] ? 0xff00U : 0) | (S.color_mask[2] ? 0xffU : 0);
    r->x0 = 0; r->y0 = 0; r->x1 = gl_frame.width; r->y1 = gl_frame.height;
    if (S.enables & E_SCISSOR) {
        int x0 = S.scissor[0], x1 = S.scissor[0] + S.scissor[2];
        int y0 = gl_frame.height - (S.scissor[1] + S.scissor[3]), y1 = gl_frame.height - S.scissor[1];
        if (x0 > r->x0) r->x0 = x0;
        if (x1 < r->x1) r->x1 = x1;
        if (y0 > r->y0) r->y0 = y0;
        if (y1 < r->y1) r->y1 = y1;
    }
    if (S.enables & E_FOG) {
        r->flags |= RF_FOG;
        for (int k = 0; k < 3; k++) r->fog_color[k] = scale255(S.fog_color[k]);
    }
    for (int u = 0; u < UNITS; u++) {
        const GlTexture *t = S.enables & (u ? E_TEXTURE1 : E_TEXTURE) ? texture_object(S.bound[u]) : 0;
        if (!t || !t->level[0].texels) continue;
        GlUnit *unit = &r->unit[u];
        r->flags |= u ? RF_TEXTURE1 : RF_TEXTURE;
        unit->tex = t; unit->levels = texture_levels(t);
        if (!unit->levels) unit->levels = 1;
        unit->min_filter = t->min_filter; unit->mag_filter = t->mag_filter;
        unit->wrap_s = t->wrap_s; unit->wrap_t = t->wrap_t;
        unit->env = S.env_mode[u]; unit->tex_alpha = t->alpha;
        for (int k = 0; k < 4; k++) unit->env_color[k] = scale255(S.env_color[u][k]);
    }
    raster_line = raster_fill;
    if (S.enables & E_OFFSET) {
        raster_fill.flags |= RF_OFFSET;
        raster_fill.offset_factor = S.offset_factor; raster_fill.offset_units = S.offset_units;
    }
}

static void project(const GlVertex *v, GlScreenVertex *o)
{
    float iw = 1.0f / v->clip[3];
    o->x = S.viewport[0] + (v->clip[0] * iw + 1) * S.viewport[2] * 0.5f;
    o->y = gl_frame.height - (S.viewport[1] + (v->clip[1] * iw + 1) * S.viewport[3] * 0.5f);
    o->z = S.depth_near + (v->clip[2] * iw + 1) * 0.5f * (S.depth_far - S.depth_near);
    o->iw = iw;
    o->r = clampf(v->col[0], 0, 1) * 255.0f; o->g = clampf(v->col[1], 0, 1) * 255.0f;
    o->b = clampf(v->col[2], 0, 1) * 255.0f; o->a = clampf(v->col[3], 0, 1) * 255.0f;
    o->s = v->s[0]; o->t = v->t[0]; o->s1 = v->s[1]; o->t1 = v->t[1]; o->f = v->fog;
}
static void screen_quad(const GlScreenVertex *a, const GlScreenVertex *b, float ox, float oy)
{
    GlScreenVertex q[4] = {*a, *a, *b, *b};
    q[0].x -= ox; q[0].y -= oy; q[1].x += ox; q[1].y += oy;
    q[2].x += ox; q[2].y += oy; q[3].x -= ox; q[3].y -= oy;
    gl_raster_triangle(&q[0], &q[1], &q[2], &raster_line);
    gl_raster_triangle(&q[0], &q[2], &q[3], &raster_line);
}
static void screen_line(const GlScreenVertex *a, const GlScreenVertex *b)
{
    float dx = b->x - a->x, dy = b->y - a->y, half = (S.line_width < 1 ? 1 : S.line_width) * 0.5f;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) screen_quad(a, b, 0, half);
    else screen_quad(a, b, half, 0);
}
static void screen_point(const GlScreenVertex *a)
{
    float half = (S.point_size < 1 ? 1 : S.point_size) * 0.5f;
    GlScreenVertex q[4] = {*a, *a, *a, *a};
    q[0].x -= half; q[0].y -= half; q[1].x += half; q[1].y -= half;
    q[2].x += half; q[2].y += half; q[3].x -= half; q[3].y += half;
    gl_raster_triangle(&q[0], &q[1], &q[2], &raster_line);
    gl_raster_triangle(&q[0], &q[2], &q[3], &raster_line);
}

/* A projected triangle: facing, culling, then fill, outline or points.
 * edges bit 0: a-b, 1: b-c, 2: c-a are polygon boundary edges. */
static void screen_triangle(const GlScreenVertex *a, const GlScreenVertex *b, const GlScreenVertex *c, unsigned int edges)
{
    float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);   /* y down */
    int front = S.front_face == GL_CCW ? area < 0 : area > 0;
    if (S.enables & E_CULL) {
        if (S.cull_face == GL_FRONT_AND_BACK) return;
        if (front ? S.cull_face == GL_FRONT : S.cull_face == GL_BACK) return;
    }
    int mode = S.poly_mode[front ? 0 : 1];
    if (mode == GL_FILL) { gl_raster_triangle(a, b, c, &raster_fill); return; }
    const GlScreenVertex *v[3] = {a, b, c};
    for (int k = 0; k < 3; k++)
        if (edges >> k & 1) {
            if (mode == GL_LINE) screen_line(v[k], v[(k + 1) % 3]);
            else screen_point(v[k]);
        }
}

/* ============================================================== clipping */
static float plane_distance(const float *c, int p)
{
    switch (p) {
    case 0: return c[3] + c[0];
    case 1: return c[3] - c[0];
    case 2: return c[3] + c[1];
    case 3: return c[3] - c[1];
    case 4: return c[3] + c[2];
    case 5: return c[3] - c[2];
    default: return c[3] - 1e-5f;
    }
}
static unsigned int outcode(const float *c)
{
    unsigned int out = 0;
    for (int p = 0; p < 7; p++) if (plane_distance(c, p) < 0) out |= 1U << p;
    return out;
}
static void lerp_vertex(GlVertex *o, const GlVertex *a, const GlVertex *b, float t)
{
    for (int k = 0; k < 4; k++) {
        o->clip[k] = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
        o->col[k] = a->col[k] + (b->col[k] - a->col[k]) * t;
    }
    for (int u = 0; u < UNITS; u++) { o->s[u] = a->s[u] + (b->s[u] - a->s[u]) * t; o->t[u] = a->t[u] + (b->t[u] - a->t[u]) * t; }
    o->fog = a->fog + (b->fog - a->fog) * t;
}
#define MAX_CLIP 16
static void triangle(const GlVertex *a, const GlVertex *b, const GlVertex *c, const float *flat, unsigned int edges)
{
    if (a->out & b->out & c->out) return;
    GlVertex buffer[2][MAX_CLIP];
    unsigned int flag[2][MAX_CLIP];
    int n = 3, cur = 0;
    buffer[0][0] = *a; buffer[0][1] = *b; buffer[0][2] = *c;
    flag[0][0] = edges & 1; flag[0][1] = edges >> 1 & 1; flag[0][2] = edges >> 2 & 1;
    if (S.shade_model == GL_FLAT)
        for (int i = 0; i < 3; i++) copy4(buffer[0][i].col, flat[0], flat[1], flat[2], flat[3]);
    unsigned int any = a->out | b->out | c->out;
    for (int p = 0; p < 7 && n >= 3; p++) {
        if (!(any >> p & 1)) continue;
        GlVertex *in = buffer[cur], *out = buffer[cur ^ 1];
        unsigned int *fin = flag[cur], *fout = flag[cur ^ 1];
        int m = 0;
        for (int i = 0; i < n && m < MAX_CLIP - 1; i++) {
            const GlVertex *u = &in[i], *w = &in[(i + 1) % n];
            float du = plane_distance(u->clip, p), dw = plane_distance(w->clip, p);
            if (du >= 0) {
                out[m] = *u; fout[m++] = fin[i];
                if (dw < 0) { lerp_vertex(&out[m], u, w, du / (du - dw)); fout[m++] = 0; }
            } else if (dw >= 0) {
                lerp_vertex(&out[m], u, w, du / (du - dw)); fout[m++] = fin[i];
            }
        }
        n = m; cur ^= 1;
    }
    if (n < 3) return;
    GlScreenVertex s[MAX_CLIP];
    for (int i = 0; i < n; i++) project(&buffer[cur][i], &s[i]);
    const unsigned int *f = flag[cur];
    for (int i = 1; i + 1 < n; i++)
        screen_triangle(&s[0], &s[i], &s[i + 1], (i == 1 ? f[0] : 0) | f[i] << 1 | (i + 2 == n ? f[n - 1] << 2 : 0));
}
static void line(const GlVertex *a, const GlVertex *b)
{
    if (a->out & b->out) return;
    float t0 = 0, t1 = 1;
    for (int p = 0; p < 7; p++) {
        float da = plane_distance(a->clip, p), db = plane_distance(b->clip, p);
        if (da < 0 && db < 0) return;
        if (da < 0) { float t = da / (da - db); if (t > t0) t0 = t; }
        else if (db < 0) { float t = da / (da - db); if (t < t1) t1 = t; }
    }
    if (t0 > t1) return;
    GlVertex u, w;
    lerp_vertex(&u, a, b, t0); lerp_vertex(&w, a, b, t1);
    if (S.shade_model == GL_FLAT) copy4(u.col, b->col[0], b->col[1], b->col[2], b->col[3]);
    GlScreenVertex su, sw;
    project(&u, &su); project(&w, &sw);
    screen_line(&su, &sw);
}
static void point(const GlVertex *a)
{
    if (a->out) return;
    GlScreenVertex s;
    project(a, &s);
    screen_point(&s);
}

/* ============================================================== vertices */
static void normalize3(float *v)
{
    float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len > 0) { len = 1.0f / sqrtf(len); v[0] *= len; v[1] *= len; v[2] *= len; }
}
static void light_vertex(const float *eye, float *out)
{
    float n[3];
    for (int r = 0; r < 3; r++) n[r] = normal_m[r * 3] * S.normal[0] + normal_m[r * 3 + 1] * S.normal[1] + normal_m[r * 3 + 2] * S.normal[2];
    normalize3(n);
    const GlMaterial *m = &S.material[0];
    const float *amb = m->ambient, *dif = m->diffuse, *spe = m->specular, *emi = m->emission;
    if (S.enables & E_COLOR_MATERIAL) {
        switch (S.cm_mode) {
        case GL_AMBIENT: amb = S.color; break;
        case GL_DIFFUSE: dif = S.color; break;
        case GL_SPECULAR: spe = S.color; break;
        case GL_EMISSION: emi = S.color; break;
        default: amb = dif = S.color;
        }
    }
    float v[3] = {eye[0], eye[1], eye[2]};
    if (eye[3] != 0 && eye[3] != 1) { v[0] /= eye[3]; v[1] /= eye[3]; v[2] /= eye[3]; }
    float view[3] = {0, 0, 1};
    if (S.local_viewer) { view[0] = -v[0]; view[1] = -v[1]; view[2] = -v[2]; normalize3(view); }
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = emi[k] + S.model_ambient[k] * amb[k];
    for (int i = 0; i < 8; i++) {
        if (!(S.enables & (E_LIGHT0 << i))) continue;
        const GlLight *l = &S.light[i];
        float L[3], att = 1;
        if (l->position[3] != 0) {
            for (int k = 0; k < 3; k++) L[k] = l->position[k] / l->position[3] - v[k];
            float d = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            if (d > 0) { L[0] /= d; L[1] /= d; L[2] /= d; }
            float q = l->attenuation[0] + l->attenuation[1] * d + l->attenuation[2] * d * d;
            att = q > 0 ? 1.0f / q : 1.0f;
            if (l->spot_cutoff != 180.0f) {
                float sd = -(L[0] * l->spot_unit[0] + L[1] * l->spot_unit[1] + L[2] * l->spot_unit[2]);
                if (sd < l->spot_cos) continue;
                if (l->spot_exponent != 0) att *= powf(sd, l->spot_exponent);
            }
        } else {
            L[0] = l->dir[0]; L[1] = l->dir[1]; L[2] = l->dir[2];
        }
        float sum[3];
        for (int k = 0; k < 3; k++) sum[k] = l->ambient[k] * amb[k];
        float ndl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        if (ndl > 0) {
            for (int k = 0; k < 3; k++) sum[k] += ndl * l->diffuse[k] * dif[k];
            float h[3] = {L[0] + view[0], L[1] + view[1], L[2] + view[2]};
            normalize3(h);
            float ndh = n[0] * h[0] + n[1] * h[1] + n[2] * h[2];
            if (ndh > 0) {
                float sp = m->shininess > 0 ? powf(ndh, m->shininess) : 1.0f;
                for (int k = 0; k < 3; k++) sum[k] += sp * l->specular[k] * spe[k];
            }
        }
        for (int k = 0; k < 3; k++) c[k] += att * sum[k];
    }
    copy4(out, c[0], c[1], c[2], dif[3]);
}
static void make_vertex(GlVertex *v, float x, float y, float z, float w)
{
    if (matrices_dirty) update_matrices();
    float obj[4] = {x, y, z, w}, eye[4];
    transform(eye, mv_stack[mv_top], obj);
    transform(v->clip, proj_stack[proj_top], eye);
    if (S.enables & E_LIGHTING) light_vertex(eye, v->col);
    else copy4(v->col, S.color[0], S.color[1], S.color[2], S.color[3]);
    for (int u = 0; u < UNITS; u++) {
        const float *tc = S.tex[u];
        if (tex_identity[u]) {
            v->s[u] = tc[0]; v->t[u] = tc[1];
            if (tc[3] != 1 && tc[3] != 0) { v->s[u] /= tc[3]; v->t[u] /= tc[3]; }
        } else {
            float t[4];
            transform(t, tex_stack[u][tex_top[u]], tc);
            float q = t[3] != 0 ? t[3] : 1;
            v->s[u] = t[0] / q; v->t[u] = t[1] / q;
        }
    }
    v->fog = 1;
    if (S.enables & E_FOG) {
        float c = eye[2] < 0 ? -eye[2] : eye[2], f;
        if (S.fog_mode == GL_LINEAR) f = S.fog_end != S.fog_start ? (S.fog_end - c) / (S.fog_end - S.fog_start) : 1;
        else if (S.fog_mode == GL_EXP2) f = expf(-(S.fog_density * c) * (S.fog_density * c));
        else f = expf(-S.fog_density * c);
        v->fog = clampf(f, 0, 1);
    }
    v->out = outcode(v->clip);
}
static void assemble(const GlVertex *v)
{
    int n = prim_n++;
    switch (prim_mode) {
    case GL_POINTS: point(v); break;
    case GL_LINES:
        if (n & 1) line(&pv[0], v); else pv[0] = *v;
        break;
    case GL_LINE_STRIP: case GL_LINE_LOOP:
        if (n == 0) pv[0] = pv[1] = *v;
        else { line(&pv[1], v); pv[1] = *v; }
        break;
    case GL_TRIANGLES:
        if (n % 3 < 2) pv[n % 3] = *v; else triangle(&pv[0], &pv[1], v, v->col, 7);
        break;
    case GL_TRIANGLE_STRIP:
        if (n < 2) { pv[n] = *v; break; }
        if (n & 1) triangle(&pv[1], &pv[0], v, v->col, 7); else triangle(&pv[0], &pv[1], v, v->col, 7);
        pv[0] = pv[1]; pv[1] = *v;
        break;
    case GL_TRIANGLE_FAN:
        if (n < 2) { pv[n] = *v; break; }
        triangle(&pv[0], &pv[1], v, v->col, 7);
        pv[1] = *v;
        break;
    case GL_QUADS:
        if (n % 4 < 3) { pv[n % 4] = *v; break; }
        triangle(&pv[0], &pv[1], &pv[2], v->col, 3);
        triangle(&pv[0], &pv[2], v, v->col, 6);
        break;
    case GL_QUAD_STRIP:
        if (n < 2) { pv[n] = *v; break; }
        if (!(n & 1)) { pv[2] = *v; break; }
        triangle(&pv[0], &pv[1], v, v->col, 3);
        triangle(&pv[0], v, &pv[2], v->col, 6);
        pv[0] = pv[2]; pv[1] = *v;
        break;
    case GL_POLYGON:   /* a fan, one triangle behind, so the closing edge is known */
        if (n < 2) { pv[n] = *v; break; }
        if (n >= 3) triangle(&pv[0], &pv[2], &pv[3], pv[0].col, (n == 3 ? 1U : 0U) | 2U);
        pv[2] = pv[1]; pv[3] = *v; pv[1] = *v;
        break;
    }
}

/* ========================================================= display lists */
enum {
    OP_BEGIN = 1, OP_END, OP_VERTEX, OP_COLOR, OP_NORMAL, OP_TEXCOORD, OP_MATRIX_MODE, OP_LOAD_IDENTITY,
    OP_LOAD_MATRIX, OP_MULT_MATRIX, OP_TRANSLATE, OP_ROTATE, OP_SCALE, OP_PUSH, OP_POP, OP_ORTHO, OP_FRUSTUM,
    OP_ENABLE, OP_DISABLE, OP_BIND_TEXTURE, OP_SHADE_MODEL, OP_BLEND_FUNC, OP_DEPTH_FUNC, OP_DEPTH_MASK,
    OP_CULL_FACE, OP_FRONT_FACE, OP_ALPHA_FUNC, OP_LINE_WIDTH, OP_POINT_SIZE, OP_POLYGON_MODE, OP_CALL_LIST,
    OP_MATERIAL, OP_LIGHT, OP_LIGHT_MODEL, OP_COLOR_MATERIAL, OP_FOG, OP_TEX_ENV, OP_CLEAR, OP_CLEAR_COLOR,
    OP_PUSH_ATTRIB, OP_POP_ATTRIB, OP_COLOR_MASK, OP_VIEWPORT, OP_SCISSOR, OP_POLYGON_OFFSET, OP_TEX_PARAMETER,
    OP_ACTIVE_TEXTURE, OP_MULTI_TEXCOORD, OP_BLEND_EQUATION, OP_BLEND_COLOR, OP_ARRAYS
};
/* A list is recorded into one scratch buffer shared by all compiles and
 * copied to an exact-size block by glEndList, so rebuilding lists does not
 * leave the heap full of outgrown buffers. */
static float *list_scratch;
static unsigned int list_scratch_cap;
static void copy_words(float *to, const float *from, unsigned int count)
{
    const unsigned int *f = (const unsigned int *)from;   /* raw words: packed colors are not floats */
    unsigned int *t = (unsigned int *)to;
    for (unsigned int i = 0; i < count; i++) t[i] = f[i];
}
/* Room for at least need words in the list being compiled. */
static int list_reserve(GlList *l, unsigned int need)
{
    if (need <= l->cap) return 1;
    unsigned int cap = l->cap ? l->cap * 2 : 4096;
    while (cap < need) cap *= 2;
    if (l->words && heap_resize(l->words, cap * 4)) { l->cap = list_scratch_cap = cap; return 1; }
    float *words = bxgl_alloc(cap * 4);
    if (!words && cap > need) words = bxgl_alloc((cap = need) * 4);
    if (!words) return 0;
    copy_words(words, l->words, l->count);
    bxgl_free(l->words);
    l->words = list_scratch = words; l->cap = list_scratch_cap = cap;
    return 1;
}
/* Returns 1 when the call must only be recorded (GL_COMPILE). */
static int record(int op, int n, const float *args)
{
    if (!compiling) return 0;
    GlList *l = compiling;
    if (!list_reserve(l, l->count + n + 1)) { set_error(GL_OUT_OF_MEMORY); return compile_mode == GL_COMPILE; }
    l->words[l->count++] = (float)(op * 32 + n);
    for (int i = 0; i < n; i++) l->words[l->count++] = args[i];
    return compile_mode == GL_COMPILE;
}
#define REC(op, n, ...) (compiling && record(op, n, (const float[]){__VA_ARGS__}))
#define REC0(op) (compiling && record(op, 0, 0))
static void execute_list(unsigned int name);

/* ================================================================ public */
static void begin_impl(GLenum mode)
{
    if (prim_mode >= 0 || !ready) { set_error(GL_INVALID_OPERATION); return; }
    if (mode > GL_POLYGON) { set_error(GL_INVALID_ENUM); return; }
    build_raster();
    prim_mode = (int)mode; prim_n = 0;
}
static void end_impl(void)
{
    if (prim_mode < 0) { set_error(GL_INVALID_OPERATION); return; }
    if (prim_mode == GL_LINE_LOOP && prim_n >= 2) line(&pv[1], &pv[0]);
    if (prim_mode == GL_POLYGON && prim_n >= 3) triangle(&pv[0], &pv[2], &pv[3], pv[0].col, (prim_n == 3 ? 1U : 0U) | 6U);
    prim_mode = -1;
}
void glBegin(GLenum mode) { if (REC(OP_BEGIN, 1, (float)mode)) return; begin_impl(mode); }
void glEnd(void) { if (REC0(OP_END)) return; end_impl(); }
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (REC(OP_VERTEX, 4, x, y, z, w)) return;
    if (prim_mode < 0) return;
    GlVertex v;
    make_vertex(&v, x, y, z, w);
    assemble(&v);
}
void glVertex2f(GLfloat x, GLfloat y) { glVertex4f(x, y, 0, 1); }
void glVertex2i(GLint x, GLint y) { glVertex4f((float)x, (float)y, 0, 1); }
void glVertex2d(GLdouble x, GLdouble y) { glVertex4f((float)x, (float)y, 0, 1); }
void glVertex2fv(const GLfloat *v) { glVertex4f(v[0], v[1], 0, 1); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex4f(x, y, z, 1); }
void glVertex3i(GLint x, GLint y, GLint z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void glVertex3fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], 1); }
void glVertex3dv(const GLdouble *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], 1); }
void glVertex4fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], v[3]); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    if (REC(OP_COLOR, 4, r, g, b, a)) return;
    copy4(S.color, r, g, b, a);
}
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1); }
void glColor3d(GLdouble r, GLdouble g, GLdouble b) { glColor4f((float)r, (float)g, (float)b, 1); }
void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a) { glColor4f((float)r, (float)g, (float)b, (float)a); }
void glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1); }
void glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, 1); }
void glColor3ubv(const GLubyte *v) { glColor3ub(v[0], v[1], v[2]); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
void glColor4ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], v[3]); }
void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (REC(OP_NORMAL, 3, x, y, z)) return;
    S.normal[0] = x; S.normal[1] = y; S.normal[2] = z;
}
void glNormal3d(GLdouble x, GLdouble y, GLdouble z) { glNormal3f((float)x, (float)y, (float)z); }
void glNormal3fv(const GLfloat *v) { glNormal3f(v[0], v[1], v[2]); }
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    if (REC(OP_TEXCOORD, 4, s, t, r, q)) return;
    copy4(S.tex[0], s, t, r, q);
}
void glMultiTexCoord4f(GLenum unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    unsigned int u = unit - GL_TEXTURE0;
    if (u >= UNITS) { if (u >= 32) set_error(GL_INVALID_ENUM); return; }
    if (REC(OP_MULTI_TEXCOORD, 5, (float)u, s, t, r, q)) return;
    copy4(S.tex[u], s, t, r, q);
}
void glMultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t) { glMultiTexCoord4f(unit, s, t, 0, 1); }
static void active_texture_impl(GLenum unit)
{
    unsigned int u = unit - GL_TEXTURE0;
    if (u >= UNITS) { set_error(GL_INVALID_ENUM); return; }
    S.active_unit = (int)u;
}
void glActiveTexture(GLenum unit) { if (REC(OP_ACTIVE_TEXTURE, 1, (float)unit)) return; active_texture_impl(unit); }
void glClientActiveTexture(GLenum unit)
{
    unsigned int u = unit - GL_TEXTURE0;
    if (u >= UNITS) { set_error(GL_INVALID_ENUM); return; }
    client_unit = (int)u;
}
void glTexCoord1f(GLfloat s) { glTexCoord4f(s, 0, 0, 1); }
void glTexCoord2f(GLfloat s, GLfloat t) { glTexCoord4f(s, t, 0, 1); }
void glTexCoord2d(GLdouble s, GLdouble t) { glTexCoord4f((float)s, (float)t, 0, 1); }
void glTexCoord2i(GLint s, GLint t) { glTexCoord4f((float)s, (float)t, 0, 1); }
void glTexCoord2fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], 0, 1); }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { glTexCoord4f(s, t, r, 1); }
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    glBegin(GL_POLYGON);
    glVertex2f(x1, y1); glVertex2f(x2, y1); glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }

/* ---------------------------------------------------------- matrices */
static void matrix_mode_impl(GLenum mode)
{
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) { set_error(GL_INVALID_ENUM); return; }
    S.matrix_mode = (int)mode;
}
void glMatrixMode(GLenum mode) { if (REC(OP_MATRIX_MODE, 1, (float)mode)) return; matrix_mode_impl(mode); }
void glLoadIdentity(void) { if (REC0(OP_LOAD_IDENTITY)) return; identity(current_matrix()); matrix_changed(); }
void glPushMatrix(void) { if (REC0(OP_PUSH)) return; push_impl(); }
void glPopMatrix(void) { if (REC0(OP_POP)) return; pop_impl(); }
void glLoadMatrixf(const GLfloat *m)
{
    if (compiling && record(OP_LOAD_MATRIX, 16, m)) return;
    copy16(current_matrix(), m); matrix_changed();
}
void glMultMatrixf(const GLfloat *m) { if (compiling && record(OP_MULT_MATRIX, 16, m)) return; mult_impl(m); }
static void transpose(float *out, const float *m) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[c * 4 + r] = m[r * 4 + c]; }
void glLoadTransposeMatrixf(const GLfloat *m) { float t[16]; transpose(t, m); glLoadMatrixf(t); }
void glMultTransposeMatrixf(const GLfloat *m) { float t[16]; transpose(t, m); glMultMatrixf(t); }
void glLoadMatrixd(const GLdouble *m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glLoadMatrixf(f); }
void glMultMatrixd(const GLdouble *m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glMultMatrixf(f); }
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) { if (REC(OP_TRANSLATE, 3, x, y, z)) return; translate_impl(x, y, z); }
void glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }
void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { if (REC(OP_ROTATE, 4, a, x, y, z)) return; rotate_impl(a, x, y, z); }
void glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)a, (float)x, (float)y, (float)z); }
void glScalef(GLfloat x, GLfloat y, GLfloat z) { if (REC(OP_SCALE, 3, x, y, z)) return; scale_impl(x, y, z); }
void glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    if (REC(OP_ORTHO, 6, (float)l, (float)r, (float)b, (float)t, (float)n, (float)f)) return;
    ortho_impl((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    if (REC(OP_FRUSTUM, 6, (float)l, (float)r, (float)b, (float)t, (float)n, (float)f)) return;
    frustum_impl((float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
}
static void viewport_impl(int x, int y, int w, int h)
{
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE); return; }
    S.viewport[0] = x; S.viewport[1] = y; S.viewport[2] = w; S.viewport[3] = h;
}
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { if (REC(OP_VIEWPORT, 4, (float)x, (float)y, (float)w, (float)h)) return; viewport_impl(x, y, w, h); }
void glDepthRange(GLclampd n, GLclampd f) { S.depth_near = clampf((float)n, 0, 1); S.depth_far = clampf((float)f, 0, 1); }

/* ------------------------------------------------------------ enables */
static unsigned int cap_bit(GLenum cap)
{
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) return E_LIGHT0 << (cap - GL_LIGHT0);
    switch (cap) {
    case GL_DEPTH_TEST: return E_DEPTH;
    case GL_CULL_FACE: return E_CULL;
    case GL_TEXTURE_2D: return S.active_unit ? E_TEXTURE1 : E_TEXTURE;
    case GL_BLEND: return E_BLEND;
    case GL_LIGHTING: return E_LIGHTING;
    case GL_FOG: return E_FOG;
    case GL_ALPHA_TEST: return E_ALPHA;
    case GL_COLOR_MATERIAL: return E_COLOR_MATERIAL;
    case GL_NORMALIZE: case GL_RESCALE_NORMAL: return E_NORMALIZE;
    case GL_SCISSOR_TEST: return E_SCISSOR;
    case GL_POLYGON_OFFSET_FILL: return E_OFFSET;
    case GL_DITHER: case GL_POINT_SMOOTH: case GL_LINE_SMOOTH: case GL_POLYGON_SMOOTH: case GL_LINE_STIPPLE:
    case GL_STENCIL_TEST: case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
    case GL_MULTISAMPLE: case GL_COLOR_SUM: case GL_AUTO_NORMAL: case GL_COLOR_LOGIC_OP: case GL_CLIP_PLANE0: case GL_CLIP_PLANE1:
        return 0;   /* accepted, not implemented */
    default: return ~0U;
    }
}
static void enable_impl(GLenum cap, int on)
{
    unsigned int bit = cap_bit(cap);
    if (bit == ~0U) { set_error(GL_INVALID_ENUM); return; }
    if (on) S.enables |= bit; else S.enables &= ~bit;
}
void glEnable(GLenum cap) { if (REC(OP_ENABLE, 1, (float)cap)) return; enable_impl(cap, 1); }
void glDisable(GLenum cap) { if (REC(OP_DISABLE, 1, (float)cap)) return; enable_impl(cap, 0); }
GLboolean glIsEnabled(GLenum cap)
{
    switch (cap) {
    case GL_VERTEX_ARRAY: return (GLboolean)va_vertex.enabled;
    case GL_COLOR_ARRAY: return (GLboolean)va_color.enabled;
    case GL_NORMAL_ARRAY: return (GLboolean)va_normal.enabled;
    case GL_TEXTURE_COORD_ARRAY: return (GLboolean)va_tex[client_unit].enabled;
    }
    unsigned int bit = cap_bit(cap);
    if (bit == ~0U) { set_error(GL_INVALID_ENUM); return GL_FALSE; }
    return (S.enables & bit) != 0;
}

/* ------------------------------------------------------- fragment state */
void glShadeModel(GLenum mode) { if (REC(OP_SHADE_MODEL, 1, (float)mode)) return; S.shade_model = (int)mode; }
void glBlendFunc(GLenum s, GLenum d) { if (REC(OP_BLEND_FUNC, 2, (float)s, (float)d)) return; S.blend_src = (int)s; S.blend_dst = (int)d; }
/* The color buffer has no alpha channel, so the alpha factors cannot matter. */
void glBlendFuncSeparate(GLenum srgb, GLenum drgb, GLenum sa, GLenum da) { (void)sa; (void)da; glBlendFunc(srgb, drgb); }
void glBlendEquation(GLenum mode)
{
    if (REC(OP_BLEND_EQUATION, 1, (float)mode)) return;
    if (mode != GL_FUNC_ADD && mode != GL_FUNC_SUBTRACT && mode != GL_FUNC_REVERSE_SUBTRACT && mode != GL_MIN && mode != GL_MAX) { set_error(GL_INVALID_ENUM); return; }
    S.blend_eq = (int)mode;
}
void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    if (REC(OP_BLEND_COLOR, 4, r, g, b, a)) return;
    copy4(S.blend_color, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), clampf(a, 0, 1));
}
void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z) { S.window_pos[0] = x; S.window_pos[1] = y; S.window_pos[2] = z; }
void glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b) { S.secondary[0] = r; S.secondary[1] = g; S.secondary[2] = b; }
void glFogCoordf(GLfloat c) { S.fog_coord = c; }
void glDepthFunc(GLenum f)
{
    if (REC(OP_DEPTH_FUNC, 1, (float)f)) return;
    if (f < GL_NEVER || f > GL_ALWAYS) { set_error(GL_INVALID_ENUM); return; }
    S.depth_func = (int)f;
}
void glDepthMask(GLboolean flag) { if (REC(OP_DEPTH_MASK, 1, (float)flag)) return; S.depth_mask = flag != 0; }
void glCullFace(GLenum mode) { if (REC(OP_CULL_FACE, 1, (float)mode)) return; S.cull_face = (int)mode; }
void glFrontFace(GLenum mode) { if (REC(OP_FRONT_FACE, 1, (float)mode)) return; S.front_face = (int)mode; }
void glAlphaFunc(GLenum f, GLclampf ref) { if (REC(OP_ALPHA_FUNC, 2, (float)f, ref)) return; S.alpha_func = (int)f; S.alpha_ref = clampf(ref, 0, 1); }
void glLineWidth(GLfloat w) { if (REC(OP_LINE_WIDTH, 1, w)) return; if (w <= 0) set_error(GL_INVALID_VALUE); else S.line_width = w; }
void glPointSize(GLfloat s) { if (REC(OP_POINT_SIZE, 1, s)) return; if (s <= 0) set_error(GL_INVALID_VALUE); else S.point_size = s; }
void glLineStipple(GLint factor, GLushort pattern) { (void)factor; (void)pattern; }
void glPolygonMode(GLenum face, GLenum mode)
{
    if (REC(OP_POLYGON_MODE, 2, (float)face, (float)mode)) return;
    if (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL) { set_error(GL_INVALID_ENUM); return; }
    if (face != GL_BACK) S.poly_mode[0] = (int)mode;
    if (face != GL_FRONT) S.poly_mode[1] = (int)mode;
}
void glPolygonOffset(GLfloat factor, GLfloat units)
{
    if (REC(OP_POLYGON_OFFSET, 2, factor, units)) return;
    S.offset_factor = factor; S.offset_units = units;
}
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (REC(OP_COLOR_MASK, 4, (float)r, (float)g, (float)b, (float)a)) return;
    S.color_mask[0] = r != 0; S.color_mask[1] = g != 0; S.color_mask[2] = b != 0; S.color_mask[3] = a != 0;
}
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (REC(OP_SCISSOR, 4, (float)x, (float)y, (float)w, (float)h)) return;
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE); return; }
    S.scissor[0] = x; S.scissor[1] = y; S.scissor[2] = w; S.scissor[3] = h;
}
void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    if (REC(OP_CLEAR_COLOR, 4, r, g, b, a)) return;
    copy4(S.clear_color, clampf(r, 0, 1), clampf(g, 0, 1), clampf(b, 0, 1), clampf(a, 0, 1));
}
void glClearDepth(GLclampd d) { S.clear_depth = clampf((float)d, 0, 1); }
void glClearDepthf(GLclampf d) { S.clear_depth = clampf(d, 0, 1); }
static void clear_impl(GLbitfield mask)
{
    if (!ready) return;
    if (prim_mode >= 0) { set_error(GL_INVALID_OPERATION); return; }
    build_raster();
    unsigned int value = (unsigned int)scale255(S.clear_color[0]) << 16 | (unsigned int)scale255(S.clear_color[1]) << 8 | (unsigned int)scale255(S.clear_color[2]);
    int color = (mask & GL_COLOR_BUFFER_BIT) != 0, depth = (mask & GL_DEPTH_BUFFER_BIT) && S.depth_mask;
    if (color || depth) gl_raster_clear(&raster_line, color, value, depth, S.clear_depth);
}
void glClear(GLbitfield mask) { if (REC(OP_CLEAR, 1, (float)mask)) return; clear_impl(mask); }
void glFlush(void) { gl_raster_flush(); }
void glFinish(void) { gl_raster_flush(); }
void glPixelStorei(GLenum name, GLint value)
{
    if (name == GL_UNPACK_ALIGNMENT || name == GL_PACK_ALIGNMENT) {
        if (value != 1 && value != 2 && value != 4 && value != 8) { set_error(GL_INVALID_VALUE); return; }
        if (name == GL_UNPACK_ALIGNMENT) unpack_alignment = value; else pack_alignment = value;
    } else if (name == GL_UNPACK_ROW_LENGTH) unpack_row_length = value < 0 ? 0 : value;
}

/* ------------------------------------------------------------ lighting */
static int light_count(GLenum name)
{
    switch (name) {
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION: case GL_EMISSION: case GL_AMBIENT_AND_DIFFUSE:
    case GL_LIGHT_MODEL_AMBIENT: case GL_FOG_COLOR: case GL_TEXTURE_ENV_COLOR: return 4;
    case GL_SPOT_DIRECTION: return 3;
    default: return 1;
    }
}
static void light_impl(GLenum light, GLenum name, const float *p)
{
    if (light < GL_LIGHT0 || light > GL_LIGHT7) { set_error(GL_INVALID_ENUM); return; }
    GlLight *l = &S.light[light - GL_LIGHT0];
    if (matrices_dirty) update_matrices();
    const float *mv = mv_stack[mv_top];
    switch (name) {
    case GL_AMBIENT: copy4(l->ambient, p[0], p[1], p[2], p[3]); break;
    case GL_DIFFUSE: copy4(l->diffuse, p[0], p[1], p[2], p[3]); break;
    case GL_SPECULAR: copy4(l->specular, p[0], p[1], p[2], p[3]); break;
    case GL_POSITION:
        transform(l->position, mv, p);
        l->dir[0] = l->position[0]; l->dir[1] = l->position[1]; l->dir[2] = l->position[2];
        normalize3(l->dir);
        break;
    case GL_SPOT_DIRECTION:
        for (int k = 0; k < 3; k++) l->spot[k] = mv[k] * p[0] + mv[4 + k] * p[1] + mv[8 + k] * p[2];
        l->spot_unit[0] = l->spot[0]; l->spot_unit[1] = l->spot[1]; l->spot_unit[2] = l->spot[2];
        normalize3(l->spot_unit);
        break;
    case GL_SPOT_EXPONENT: l->spot_exponent = p[0]; break;
    case GL_SPOT_CUTOFF:
        if ((p[0] < 0 || p[0] > 90) && p[0] != 180) { set_error(GL_INVALID_VALUE); return; }
        l->spot_cutoff = p[0]; l->spot_cos = cosf(p[0] * (float)(M_PI / 180.0));
        break;
    case GL_CONSTANT_ATTENUATION: l->attenuation[0] = p[0]; break;
    case GL_LINEAR_ATTENUATION: l->attenuation[1] = p[0]; break;
    case GL_QUADRATIC_ATTENUATION: l->attenuation[2] = p[0]; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
static void pad4(float *out, const float *in, int n) { for (int i = 0; i < 4; i++) out[i] = i < n ? in[i] : 0; }
void glLightfv(GLenum light, GLenum name, const GLfloat *values)
{
    float p[4]; pad4(p, values, light_count(name));
    if (REC(OP_LIGHT, 6, (float)light, (float)name, p[0], p[1], p[2], p[3])) return;
    light_impl(light, name, p);
}
void glLightf(GLenum light, GLenum name, GLfloat value) { glLightfv(light, name, &value); }
void glLighti(GLenum light, GLenum name, GLint value) { float v = (float)value; glLightfv(light, name, &v); }
void glGetLightfv(GLenum light, GLenum name, GLfloat *out)
{
    if (light < GL_LIGHT0 || light > GL_LIGHT7) { set_error(GL_INVALID_ENUM); return; }
    const GlLight *l = &S.light[light - GL_LIGHT0];
    const float *src = name == GL_AMBIENT ? l->ambient : name == GL_DIFFUSE ? l->diffuse : name == GL_SPECULAR ? l->specular
                     : name == GL_POSITION ? l->position : name == GL_SPOT_DIRECTION ? l->spot : 0;
    if (src) { for (int k = 0; k < light_count(name); k++) out[k] = src[k]; return; }
    switch (name) {
    case GL_SPOT_EXPONENT: out[0] = l->spot_exponent; break;
    case GL_SPOT_CUTOFF: out[0] = l->spot_cutoff; break;
    case GL_CONSTANT_ATTENUATION: out[0] = l->attenuation[0]; break;
    case GL_LINEAR_ATTENUATION: out[0] = l->attenuation[1]; break;
    case GL_QUADRATIC_ATTENUATION: out[0] = l->attenuation[2]; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
static void light_model_impl(GLenum name, const float *p)
{
    switch (name) {
    case GL_LIGHT_MODEL_AMBIENT: copy4(S.model_ambient, p[0], p[1], p[2], p[3]); break;
    case GL_LIGHT_MODEL_TWO_SIDE: S.two_side = p[0] != 0; break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: S.local_viewer = p[0] != 0; break;
    case GL_LIGHT_MODEL_COLOR_CONTROL: break;
    default: set_error(GL_INVALID_ENUM);
    }
}
void glLightModelfv(GLenum name, const GLfloat *values)
{
    float p[4]; pad4(p, values, light_count(name));
    if (REC(OP_LIGHT_MODEL, 5, (float)name, p[0], p[1], p[2], p[3])) return;
    light_model_impl(name, p);
}
void glLightModelf(GLenum name, GLfloat value) { glLightModelfv(name, &value); }
void glLightModeli(GLenum name, GLint value) { float v = (float)value; glLightModelfv(name, &v); }
static void material_impl(GLenum face, GLenum name, const float *p)
{
    for (int side = 0; side < 2; side++) {
        if ((side == 0 && face == GL_BACK) || (side == 1 && face == GL_FRONT)) continue;
        GlMaterial *m = &S.material[side];
        switch (name) {
        case GL_AMBIENT: copy4(m->ambient, p[0], p[1], p[2], p[3]); break;
        case GL_DIFFUSE: copy4(m->diffuse, p[0], p[1], p[2], p[3]); break;
        case GL_AMBIENT_AND_DIFFUSE: copy4(m->ambient, p[0], p[1], p[2], p[3]); copy4(m->diffuse, p[0], p[1], p[2], p[3]); break;
        case GL_SPECULAR: copy4(m->specular, p[0], p[1], p[2], p[3]); break;
        case GL_EMISSION: copy4(m->emission, p[0], p[1], p[2], p[3]); break;
        case GL_SHININESS: m->shininess = clampf(p[0], 0, 128); break;
        default: set_error(GL_INVALID_ENUM); return;
        }
    }
}
void glMaterialfv(GLenum face, GLenum name, const GLfloat *values)
{
    float p[4]; pad4(p, values, light_count(name));
    if (REC(OP_MATERIAL, 6, (float)face, (float)name, p[0], p[1], p[2], p[3])) return;
    material_impl(face, name, p);
}
void glMaterialf(GLenum face, GLenum name, GLfloat value) { glMaterialfv(face, name, &value); }
void glMateriali(GLenum face, GLenum name, GLint value) { float v = (float)value; glMaterialfv(face, name, &v); }
void glColorMaterial(GLenum face, GLenum mode)
{
    if (REC(OP_COLOR_MATERIAL, 2, (float)face, (float)mode)) return;
    S.cm_face = (int)face; S.cm_mode = (int)mode;
}
static void fog_impl(GLenum name, const float *p)
{
    switch (name) {
    case GL_FOG_MODE: S.fog_mode = (int)p[0]; break;
    case GL_FOG_DENSITY: S.fog_density = p[0]; break;
    case GL_FOG_START: S.fog_start = p[0]; break;
    case GL_FOG_END: S.fog_end = p[0]; break;
    case GL_FOG_COLOR: copy4(S.fog_color, p[0], p[1], p[2], p[3]); break;
    case GL_FOG_INDEX: break;
    default: set_error(GL_INVALID_ENUM);
    }
}
void glFogfv(GLenum name, const GLfloat *values)
{
    float p[4]; pad4(p, values, light_count(name));
    if (REC(OP_FOG, 5, (float)name, p[0], p[1], p[2], p[3])) return;
    fog_impl(name, p);
}
void glFogf(GLenum name, GLfloat value) { glFogfv(name, &value); }
void glFogi(GLenum name, GLint value) { float v = (float)value; glFogfv(name, &v); }

/* ------------------------------------------------------------ textures */
void glGenTextures(GLsizei n, GLuint *out)
{
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    unsigned int name = 1;
    for (int i = 0; i < n; i++) {
        while (name < MAX_TEXTURES && texture_names[name]) name++;
        if (name >= MAX_TEXTURES) { set_error(GL_OUT_OF_MEMORY); out[i] = 0; continue; }
        texture_names[name] = 1;
        out[i] = name;
    }
}
static void free_levels(GlTexture *t)
{
    for (int k = 0; k < GL_TEX_LEVELS; k++) { bxgl_free(t->level[k].texels); t->level[k].texels = 0; }
}
void glDeleteTextures(GLsizei n, const GLuint *names)
{
    gl_raster_flush();
    for (int i = 0; i < n; i++) {
        unsigned int name = names[i];
        if (!name || name >= MAX_TEXTURES) continue;
        if (textures[name]) { free_levels(textures[name]); bxgl_free(textures[name]); textures[name] = 0; }
        texture_names[name] = 0;
        for (int u = 0; u < UNITS; u++) if (S.bound[u] == name) S.bound[u] = 0;
    }
}
static void bind_texture_impl(GLenum target, unsigned int name)
{
    if (target != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM); return; }
    if (name >= MAX_TEXTURES) { set_error(GL_INVALID_VALUE); return; }
    if (texture_object(name)) S.bound[S.active_unit] = name;
}
void glBindTexture(GLenum target, GLuint name) { if (REC(OP_BIND_TEXTURE, 2, (float)target, (float)name)) return; bind_texture_impl(target, name); }
GLboolean glIsTexture(GLuint name) { return name && name < MAX_TEXTURES && textures[name]; }

static int components(GLenum format)
{
    switch (format) {
    case GL_RGB: case GL_BGR: return 3;
    case GL_RGBA: case GL_BGRA: return 4;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_LUMINANCE: case GL_ALPHA: case GL_RED: case GL_GREEN: case GL_BLUE: return 1;
    default: return 0;
    }
}
static int has_alpha(GLint internal)
{
    return internal == 2 || internal == 4 || internal == GL_RGBA || internal == GL_RGBA8 || internal == GL_BGRA ||
           internal == GL_LUMINANCE_ALPHA || internal == GL_ALPHA || internal == 0x8057 /* RGB5_A1 */ ||
           internal == 0x8056 /* RGBA4 */ || internal == 0x803C /* ALPHA8 */ || internal == 0x8045 /* LUMINANCE8_ALPHA8 */;
}
static int packed_type(GLenum type)
{
    return type == GL_UNSIGNED_INT_8_8_8_8 || type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_SHORT_5_6_5 ||
           type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1;
}
static int pixel_size(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_INT_8_8_8_8 || type == GL_UNSIGNED_INT_8_8_8_8_REV) return 4;
    if (packed_type(type)) return 2;
    return components(format) * (type == GL_FLOAT ? 4 : 1);
}
/* One source pixel as 0xAARRGGBB. */
static unsigned int fetch(const unsigned char *p, GLenum format, GLenum type)
{
    int n = components(format), c[4] = {0, 0, 0, 255};
    if (type == GL_UNSIGNED_INT_8_8_8_8 || type == GL_UNSIGNED_INT_8_8_8_8_REV) {
        unsigned int w = (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
        for (int i = 0; i < 4; i++) c[i] = type == GL_UNSIGNED_INT_8_8_8_8 ? (int)(w >> (24 - 8 * i) & 255) : (int)(w >> (8 * i) & 255);
        n = 4;
    } else if (packed_type(type)) {
        unsigned int w = (unsigned int)p[0] | (unsigned int)p[1] << 8;
        if (type == GL_UNSIGNED_SHORT_5_6_5) { c[0] = (int)(w >> 11) * 255 / 31; c[1] = (int)(w >> 5 & 63) * 255 / 63; c[2] = (int)(w & 31) * 255 / 31; }
        else if (type == GL_UNSIGNED_SHORT_4_4_4_4) for (int i = 0; i < 4; i++) c[i] = (int)(w >> (12 - 4 * i) & 15) * 17;
        else { c[0] = (int)(w >> 11) * 255 / 31; c[1] = (int)(w >> 6 & 31) * 255 / 31; c[2] = (int)(w >> 1 & 31) * 255 / 31; c[3] = w & 1 ? 255 : 0; }
    } else for (int i = 0; i < n; i++)
        c[i] = type == GL_FLOAT ? gl_iround(clampf(((const float *)p)[i], 0, 1) * 255.0f) : p[i];
    int r, g, b, a;
    switch (format) {
    case GL_BGR: case GL_BGRA: r = c[2]; g = c[1]; b = c[0]; a = c[3]; break;
    case GL_LUMINANCE: r = g = b = c[0]; a = 255; break;
    case GL_LUMINANCE_ALPHA: r = g = b = c[0]; a = c[1]; break;
    case GL_ALPHA: r = g = b = 255; a = c[0]; break;
    case GL_RED: r = c[0]; g = b = 0; a = 255; break;
    case GL_GREEN: g = c[0]; r = b = 0; a = 255; break;
    case GL_BLUE: b = c[0]; r = g = 0; a = 255; break;
    default: r = c[0]; g = c[1]; b = c[2]; a = c[3];
    }
    return (unsigned int)a << 24 | (unsigned int)r << 16 | (unsigned int)g << 8 | (unsigned int)b;
}
static void unpack(unsigned int *out, int out_stride, int width, int height, GLenum format, GLenum type, const void *pixels, int alpha)
{
    int size = pixel_size(format, type);
    int row = (unpack_row_length ? unpack_row_length : width) * size;
    row = (row + unpack_alignment - 1) / unpack_alignment * unpack_alignment;
    for (int y = 0; y < height; y++) {
        const unsigned char *p = (const unsigned char *)pixels + y * row;
        for (int x = 0; x < width; x++) {
            unsigned int v = fetch(p + x * size, format, type);
            out[y * out_stride + x] = alpha ? v : v | 0xff000000U;
        }
    }
}
static int pot_mask(int v) { return v > 0 && !(v & (v - 1)) ? v - 1 : -1; }
/* GL_GENERATE_MIPMAP: box-filtered levels below level 0. */
static void generate_mipmaps(GlTexture *t)
{
    for (int k = 1; k < GL_TEX_LEVELS; k++) {
        const GlLevel *src = &t->level[k - 1];
        if (src->w <= 1 && src->h <= 1) {
            for (int j = k; j < GL_TEX_LEVELS; j++) { bxgl_free(t->level[j].texels); t->level[j].texels = 0; }
            return;
        }
        int w = src->w > 1 ? src->w / 2 : 1, h = src->h > 1 ? src->h / 2 : 1;
        GlLevel *l = &t->level[k];
        if (!l->texels || l->w != w || l->h != h) {
            bxgl_free(l->texels);
            l->texels = bxgl_alloc((unsigned int)w * h * 4);
            if (!l->texels) return;
            l->w = w; l->h = h; l->wmask = pot_mask(w); l->hmask = pot_mask(h);
        }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int x0 = x * 2 < src->w ? x * 2 : src->w - 1, y0 = y * 2 < src->h ? y * 2 : src->h - 1;
                int x1 = x0 + 1 < src->w ? x0 + 1 : x0, y1 = y0 + 1 < src->h ? y0 + 1 : y0;
                unsigned int p[4] = {src->texels[y0 * src->w + x0], src->texels[y0 * src->w + x1], src->texels[y1 * src->w + x0], src->texels[y1 * src->w + x1]};
                unsigned int out = 0;
                for (int c = 0; c < 32; c += 8) {
                    unsigned int sum = 2;
                    for (int i = 0; i < 4; i++) sum += p[i] >> c & 255;
                    out |= (sum / 4) << c;
                }
                l->texels[y * w + x] = out;
            }
    }
}
void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (target != GL_TEXTURE_2D || !components(format) || (type != GL_UNSIGNED_BYTE && type != GL_FLOAT && !packed_type(type))) { set_error(GL_INVALID_ENUM); return; }
    if (level < 0 || level >= GL_TEX_LEVELS || width < 0 || height < 0 || width > 4096 || height > 4096 || border) { set_error(GL_INVALID_VALUE); return; }
    GlTexture *t = bound_texture();
    if (!t) return;
    gl_raster_flush();
    GlLevel *l = &t->level[level];
    bxgl_free(l->texels);
    l->texels = 0;
    if (!width || !height) return;
    l->texels = bxgl_alloc((unsigned int)width * height * 4);
    if (!l->texels) { set_error(GL_OUT_OF_MEMORY); return; }
    l->w = width; l->h = height; l->wmask = pot_mask(width); l->hmask = pot_mask(height);
    if (level == 0) t->alpha = has_alpha(internal);
    if (pixels) unpack(l->texels, width, width, height, format, type, pixels, t->alpha);
    else for (int i = 0; i < width * height; i++) l->texels[i] = 0xff000000U;
    if (level == 0 && t->generate_mipmap) generate_mipmaps(t);
}
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels)
{
    if (target != GL_TEXTURE_2D || !components(format)) { set_error(GL_INVALID_ENUM); return; }
    GlTexture *t = bound_texture();
    if (!t || level < 0 || level >= GL_TEX_LEVELS) { set_error(GL_INVALID_VALUE); return; }
    GlLevel *l = &t->level[level];
    if (!l->texels || x < 0 || y < 0 || x + width > l->w || y + height > l->h) { set_error(GL_INVALID_VALUE); return; }
    gl_raster_flush();
    if (pixels) unpack(l->texels + y * l->w + x, l->w, width, height, format, type, pixels, t->alpha);
    if (level == 0 && t->generate_mipmap) generate_mipmaps(t);
}
void glCopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    glTexImage2D(target, level, (GLint)internal, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    GlTexture *t = bound_texture();
    if (!t || !t->level[level].texels) return;
    for (int j = 0; j < height; j++)
        for (int i = 0; i < width; i++) {
            int cx = x + i, cy = gl_frame.height - 1 - (y + j);
            unsigned int v = cx >= 0 && cy >= 0 && cx < gl_frame.width && cy < gl_frame.height ? gl_frame.color[cy * gl_frame.width + cx] : 0;
            t->level[level].texels[j * width + i] = v | 0xff000000U;
        }
}
static void tex_parameter_impl(GLenum target, GLenum name, int value)
{
    GlTexture *t = target == GL_TEXTURE_2D ? bound_texture() : 0;
    if (!t) { set_error(GL_INVALID_ENUM); return; }
    switch (name) {
    case GL_TEXTURE_MIN_FILTER: t->min_filter = value; break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = value; break;
    case GL_TEXTURE_WRAP_S: t->wrap_s = value == GL_REPEAT || value == GL_MIRRORED_REPEAT ? value : GL_CLAMP_TO_EDGE; break;
    case GL_TEXTURE_WRAP_T: t->wrap_t = value == GL_REPEAT || value == GL_MIRRORED_REPEAT ? value : GL_CLAMP_TO_EDGE; break;
    case GL_GENERATE_MIPMAP: t->generate_mipmap = value != 0; if (value && t->level[0].texels) generate_mipmaps(t); break;
    case GL_TEXTURE_MAX_LEVEL: t->max_level = value < 0 ? 0 : value; break;
    default: break;
    }
}
void glTexParameteri(GLenum target, GLenum name, GLint value)
{
    if (REC(OP_TEX_PARAMETER, 3, (float)target, (float)name, (float)value)) return;
    tex_parameter_impl(target, name, value);
}
void glTexParameterf(GLenum target, GLenum name, GLfloat value) { glTexParameteri(target, name, (GLint)value); }
static void tex_env_impl(GLenum name, const float *p)
{
    if (name == GL_TEXTURE_ENV_MODE) S.env_mode[S.active_unit] = (int)p[0];
    else if (name == GL_TEXTURE_ENV_COLOR) copy4(S.env_color[S.active_unit], p[0], p[1], p[2], p[3]);
    else if (name != GL_TEXTURE_LOD_BIAS) set_error(GL_INVALID_ENUM);
}
void glTexEnvfv(GLenum target, GLenum name, const GLfloat *values)
{
    (void)target;
    float p[4]; pad4(p, values, light_count(name));
    if (REC(OP_TEX_ENV, 5, (float)name, p[0], p[1], p[2], p[3])) return;
    tex_env_impl(name, p);
}
void glTexEnvf(GLenum target, GLenum name, GLfloat value) { glTexEnvfv(target, name, &value); }
void glTexEnvi(GLenum target, GLenum name, GLint value) { float v = (float)value; glTexEnvfv(target, name, &v); }
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum name, GLint *value)
{
    GlTexture *t = target == GL_TEXTURE_2D ? bound_texture() : 0;
    if (!t || level < 0 || level >= GL_TEX_LEVELS) { set_error(GL_INVALID_VALUE); return; }
    *value = name == GL_TEXTURE_WIDTH ? t->level[level].w : name == GL_TEXTURE_HEIGHT ? t->level[level].h : 0;
    if (!t->level[level].texels) *value = 0;
}

/* --------------------------------------------------------- vertex arrays */
static void set_array(GlArray *a, int size, GLenum type, GLsizei stride, const void *pointer)
{
    int bytes = type == GL_DOUBLE ? 8 : type == GL_FLOAT || type == GL_INT || type == GL_UNSIGNED_INT ? 4
              : type == GL_SHORT || type == GL_UNSIGNED_SHORT ? 2 : 1;
    a->size = size; a->type = (int)type; a->stride = stride ? stride : size * bytes; a->pointer = pointer;
    a->buffer = array_buffer;
}
static const unsigned char *array_base(const GlArray *a)
{
    if (!a->buffer) return a->pointer;
    const GlBuffer *b = a->buffer < MAX_BUFFERS ? buffers[a->buffer] : 0;
    return b && b->data ? b->data + (unsigned long)a->pointer : 0;
}
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { set_array(&va_vertex, size, type, stride, p); }
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { set_array(&va_color, size, type, stride, p); }
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *p) { set_array(&va_normal, 3, type, stride, p); }
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { set_array(&va_tex[client_unit], size, type, stride, p); }
static GlArray *client_array(GLenum array)
{
    switch (array) {
    case GL_VERTEX_ARRAY: return &va_vertex;
    case GL_COLOR_ARRAY: return &va_color;
    case GL_NORMAL_ARRAY: return &va_normal;
    case GL_TEXTURE_COORD_ARRAY: return &va_tex[client_unit];
    default: set_error(GL_INVALID_ENUM); return 0;
    }
}
void glEnableClientState(GLenum array) { GlArray *a = client_array(array); if (a) a->enabled = 1; }
void glDisableClientState(GLenum array) { GlArray *a = client_array(array); if (a) a->enabled = 0; }
static void read_array(const GlArray *a, int index, float *out, int normalized)
{
    const unsigned char *base = array_base(a);
    if (!base) return;
    const unsigned char *p = base + index * a->stride;
    for (int i = 0; i < a->size && i < 4; i++) {
        float v;
        switch (a->type) {
        case GL_DOUBLE: v = (float)((const double *)p)[i]; break;
        case GL_INT: v = (float)((const int *)p)[i]; if (normalized) v /= 2147483647.0f; break;
        case GL_UNSIGNED_INT: v = (float)((const unsigned int *)p)[i]; if (normalized) v /= 4294967295.0f; break;
        case GL_SHORT: v = ((const short *)p)[i]; if (normalized) v /= 32767.0f; break;
        case GL_UNSIGNED_SHORT: v = ((const unsigned short *)p)[i]; if (normalized) v /= 65535.0f; break;
        case GL_BYTE: v = ((const signed char *)p)[i]; if (normalized) v /= 127.0f; break;
        case GL_UNSIGNED_BYTE: v = p[i]; if (normalized) v /= 255.0f; break;
        default: v = ((const float *)p)[i];
        }
        out[i] = v;
    }
}
void glArrayElement(GLint i)
{
    if (va_color.enabled) { float c[4] = {0, 0, 0, 1}; read_array(&va_color, i, c, 1); glColor4f(c[0], c[1], c[2], c[3]); }
    if (va_normal.enabled) { float n[3] = {0, 0, 1}; read_array(&va_normal, i, n, 1); glNormal3f(n[0], n[1], n[2]); }
    if (va_tex[0].enabled) { float t[4] = {0, 0, 0, 1}; read_array(&va_tex[0], i, t, 0); glTexCoord4f(t[0], t[1], t[2], t[3]); }
    if (va_tex[1].enabled) { float t[4] = {0, 0, 0, 1}; read_array(&va_tex[1], i, t, 0); glMultiTexCoord4f(GL_TEXTURE1, t[0], t[1], t[2], t[3]); }
    if (va_vertex.enabled) { float v[4] = {0, 0, 0, 1}; read_array(&va_vertex, i, v, 0); glVertex4f(v[0], v[1], v[2], v[3]); }
}
/* Compiled vertex arrays: a display list stores the vertices themselves,
 * packed (position, RGBA8 color, normal, two texture coordinates). */
/* Packed vertex layout: flags in bits 0-3, the position size in bits 4-6,
 * texture coordinate sizes (1-4) for units 0 and 1 in bits 8-10 and 12-14. */
enum { PK_COLOR = 1, PK_NORMAL = 2, PK_TEX0 = 4, PK_TEX1 = 8 };
#define PK_SIZE(layout) ((int)((layout) >> 4 & 7))
#define PK_TEX_SIZE(layout, unit) ((int)((layout) >> (8 + 4 * (unit)) & 7))
static int layout_words(unsigned int layout)
{
    return PK_SIZE(layout) + (layout & PK_COLOR ? 1 : 0) + (layout & PK_NORMAL ? 3 : 0) + PK_TEX_SIZE(layout, 0) + PK_TEX_SIZE(layout, 1);
}
static int tex_size(const GlArray *a) { return a->size < 1 ? 1 : a->size > 4 ? 4 : a->size; }
static unsigned int pack_layout(int *words)
{
    unsigned int layout = 0;
    int n = va_vertex.size < 2 ? 2 : va_vertex.size > 4 ? 4 : va_vertex.size;
    layout |= (unsigned int)n << 4;
    if (va_color.enabled) { layout |= PK_COLOR; n++; }
    if (va_normal.enabled) { layout |= PK_NORMAL; n += 3; }
    for (int u = 0; u < 2; u++)
        if (va_tex[u].enabled) { layout |= (u ? PK_TEX1 : PK_TEX0) | (unsigned int)tex_size(&va_tex[u]) << (8 + 4 * u); n += tex_size(&va_tex[u]); }
    *words = n;
    return layout;
}
static int pack_vertex(int index, float *out)
{
    float v[4] = {0, 0, 0, 1};
    int n = va_vertex.size < 2 ? 2 : va_vertex.size > 4 ? 4 : va_vertex.size, at = 0;
    read_array(&va_vertex, index, v, 0);
    for (int k = 0; k < n; k++) out[at++] = v[k];
    if (va_color.enabled) {
        float c[4] = {0, 0, 0, 1};
        read_array(&va_color, index, c, 1);
        unsigned int w = 0;
        for (int k = 0; k < 4; k++) w |= (unsigned int)scale255(c[k]) << (8 * k);
        ((unsigned int *)out)[at++] = w;
    }
    if (va_normal.enabled) { float nn[3] = {0, 0, 1}; read_array(&va_normal, index, nn, 1); out[at++] = nn[0]; out[at++] = nn[1]; out[at++] = nn[2]; }
    for (int u = 0; u < UNITS; u++)
        if (va_tex[u].enabled) { float t[4] = {0, 0, 0, 1}; read_array(&va_tex[u], index, t, 0); for (int k = 0; k < tex_size(&va_tex[u]); k++) out[at++] = t[k]; }
    return at;
}
static void unpack_vertex(unsigned int layout, const float *in)
{
    int n = PK_SIZE(layout), at = n;
    if (layout & PK_COLOR) {
        unsigned int w = ((const unsigned int *)in)[at++];
        copy4(S.color, (w & 255) / 255.0f, (w >> 8 & 255) / 255.0f, (w >> 16 & 255) / 255.0f, (w >> 24) / 255.0f);
    }
    if (layout & PK_NORMAL) { S.normal[0] = in[at]; S.normal[1] = in[at + 1]; S.normal[2] = in[at + 2]; at += 3; }
    for (int u = 0; u < 2; u++) {
        int k = PK_TEX_SIZE(layout, u);
        if (!k) continue;
        copy4(S.tex[u], in[at], k > 1 ? in[at + 1] : 0, k > 2 ? in[at + 2] : 0, k > 3 ? in[at + 3] : 1);
        at += k;
    }
    if (prim_mode >= 0) {
        GlVertex v;
        make_vertex(&v, in[0], in[1], n > 2 ? in[2] : 0, n > 3 ? in[3] : 1);
        assemble(&v);
    }
}
static int index_at(GLenum type, const void *indices, int i)
{
    return type == GL_UNSIGNED_BYTE ? ((const unsigned char *)indices)[i]
         : type == GL_UNSIGNED_SHORT ? ((const unsigned short *)indices)[i] : (int)((const unsigned int *)indices)[i];
}
/* Record count vertices (from indices, or first..first+count-1) as one OP_ARRAYS. */
static int record_arrays(GLenum mode, int first, int count, GLenum type, const void *indices)
{
    if (!va_vertex.enabled || count <= 0) return compile_mode == GL_COMPILE;
    int words;
    unsigned int layout = pack_layout(&words);
    unsigned int total = 4 + (unsigned int)count * (unsigned int)words;
    GlList *l = compiling;
    if (!list_reserve(l, l->count + total)) { set_error(GL_OUT_OF_MEMORY); return compile_mode == GL_COMPILE; }
    float *w = l->words + l->count;
    w[0] = (float)(OP_ARRAYS * 32 + 3);
    ((unsigned int *)w)[1] = mode;
    ((unsigned int *)w)[2] = (unsigned int)count;
    ((unsigned int *)w)[3] = layout;
    float *out = w + 4;
    for (int i = 0; i < count; i++) out += pack_vertex(indices ? index_at(type, indices, i) : first + i, out);
    l->count += total;
    return compile_mode == GL_COMPILE;
}
void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (compiling && record_arrays(mode, first, count, 0, 0)) return;
    begin_impl(mode);
    for (int i = 0; i < count; i++) {
        int k = first + i;
        if (va_color.enabled) { float c[4] = {0, 0, 0, 1}; read_array(&va_color, k, c, 1); copy4(S.color, c[0], c[1], c[2], c[3]); }
        if (va_normal.enabled) { float n[3] = {0, 0, 1}; read_array(&va_normal, k, n, 1); S.normal[0] = n[0]; S.normal[1] = n[1]; S.normal[2] = n[2]; }
        for (int u = 0; u < UNITS; u++)
            if (va_tex[u].enabled) { float t[4] = {0, 0, 0, 1}; read_array(&va_tex[u], k, t, 0); copy4(S.tex[u], t[0], t[1], t[2], t[3]); }
        if (va_vertex.enabled && prim_mode >= 0) {
            float v[4] = {0, 0, 0, 1};
            read_array(&va_vertex, k, v, 0);
            GlVertex vx;
            make_vertex(&vx, v[0], v[1], v[2], v[3]);
            assemble(&vx);
        }
    }
    end_impl();
}
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    for (int i = 0; i < primcount; i++) glDrawArrays(mode, first[i], count[i]);
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) { set_error(GL_INVALID_ENUM); return; }
    if (element_buffer) {
        const GlBuffer *b = element_buffer < MAX_BUFFERS ? buffers[element_buffer] : 0;
        if (!b || !b->data) { set_error(GL_INVALID_OPERATION); return; }
        indices = b->data + (unsigned long)indices;
    }
    if (compiling && record_arrays(mode, 0, count, type, indices)) return;
    glBegin(mode);
    for (int i = 0; i < count; i++) glArrayElement(index_at(type, indices, i));
    glEnd();
}
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (end < start) { set_error(GL_INVALID_VALUE); return; }
    glDrawElements(mode, count, type, indices);
}

/* ---------------------------------------------------- buffer objects (1.5) */
void glGenBuffers(GLsizei n, GLuint *out)
{
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    unsigned int name = 1;
    for (int i = 0; i < n; i++) {
        while (name < MAX_BUFFERS && buffer_names[name]) name++;
        if (name >= MAX_BUFFERS) { set_error(GL_OUT_OF_MEMORY); out[i] = 0; continue; }
        buffer_names[name] = 1;
        out[i] = name;
    }
}
void glDeleteBuffers(GLsizei n, const GLuint *names)
{
    gl_raster_flush();
    for (int i = 0; i < n; i++) {
        unsigned int name = names[i];
        if (!name || name >= MAX_BUFFERS) continue;
        if (buffers[name]) { bxgl_free(buffers[name]->data); bxgl_free(buffers[name]); buffers[name] = 0; }
        buffer_names[name] = 0;
        if (array_buffer == name) array_buffer = 0;
        if (element_buffer == name) element_buffer = 0;
    }
}
static GlBuffer *buffer_object(unsigned int name)
{
    if (!name || name >= MAX_BUFFERS) return 0;
    if (!buffers[name]) {
        buffers[name] = bxgl_alloc(sizeof(GlBuffer));
        if (!buffers[name]) { set_error(GL_OUT_OF_MEMORY); return 0; }
        buffers[name]->usage = GL_STATIC_DRAW;
        buffer_names[name] = 1;
    }
    return buffers[name];
}
void glBindBuffer(GLenum target, GLuint name)
{
    if (name >= MAX_BUFFERS) { set_error(GL_INVALID_VALUE); return; }
    if (name) buffer_object(name);
    if (target == GL_ARRAY_BUFFER) array_buffer = name;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) element_buffer = name;
    else set_error(GL_INVALID_ENUM);
}
GLboolean glIsBuffer(GLuint name) { return name && name < MAX_BUFFERS && buffers[name]; }
static GlBuffer *target_buffer(GLenum target)
{
    unsigned int name = target == GL_ARRAY_BUFFER ? array_buffer : target == GL_ELEMENT_ARRAY_BUFFER ? element_buffer : ~0U;
    if (name == ~0U) { set_error(GL_INVALID_ENUM); return 0; }
    GlBuffer *b = buffer_object(name);
    if (!b) set_error(GL_INVALID_OPERATION);
    return b;
}
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    if (size < 0) { set_error(GL_INVALID_VALUE); return; }
    GlBuffer *b = target_buffer(target);
    if (!b) return;
    gl_raster_flush();
    if ((unsigned int)size != b->size || !b->data) {
        bxgl_free(b->data);
        b->data = size ? bxgl_alloc((unsigned int)size) : 0;
        if (size && !b->data) { b->size = 0; set_error(GL_OUT_OF_MEMORY); return; }
        b->size = (unsigned int)size;
    }
    if (data) { const unsigned char *in = data; for (GLsizeiptr i = 0; i < size; i++) b->data[i] = in[i]; }
    b->usage = usage;
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GlBuffer *b = target_buffer(target);
    if (!b) return;
    if (offset < 0 || size < 0 || (unsigned long)offset + (unsigned long)size > b->size) { set_error(GL_INVALID_VALUE); return; }
    gl_raster_flush();
    const unsigned char *in = data;
    for (GLsizeiptr i = 0; i < size; i++) b->data[offset + i] = in[i];
}
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data)
{
    GlBuffer *b = target_buffer(target);
    if (!b) return;
    if (offset < 0 || size < 0 || (unsigned long)offset + (unsigned long)size > b->size) { set_error(GL_INVALID_VALUE); return; }
    unsigned char *out = data;
    for (GLsizeiptr i = 0; i < size; i++) out[i] = b->data[offset + i];
}
void glGetBufferParameteriv(GLenum target, GLenum name, GLint *value)
{
    GlBuffer *b = target_buffer(target);
    if (!b) return;
    switch (name) {
    case GL_BUFFER_SIZE: *value = (GLint)b->size; break;
    case GL_BUFFER_USAGE: *value = (GLint)b->usage; break;
    case GL_BUFFER_ACCESS: *value = GL_READ_WRITE; break;
    case GL_BUFFER_MAPPED: *value = 0; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
GLvoid *glMapBuffer(GLenum target, GLenum access)
{
    (void)access;
    GlBuffer *b = target_buffer(target);
    if (!b) return 0;
    gl_raster_flush();
    return b->data;
}
GLboolean glUnmapBuffer(GLenum target) { return target_buffer(target) != 0; }

/* ------------------------------------------------- occlusion queries (1.5) */
static void query_add(unsigned int id, unsigned int samples) { if (id < MAX_QUERIES) queries[id].result += samples; }
void glGenQueries(GLsizei n, GLuint *out)
{
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    unsigned int name = 1;
    for (int i = 0; i < n; i++) {
        while (name < MAX_QUERIES && queries[name].used) name++;
        if (name >= MAX_QUERIES) { set_error(GL_OUT_OF_MEMORY); out[i] = 0; continue; }
        queries[name].used = 1; queries[name].result = 0; queries[name].active = 0;
        out[i] = name;
    }
}
void glDeleteQueries(GLsizei n, const GLuint *ids)
{
    for (int i = 0; i < n; i++) {
        unsigned int id = ids[i];
        if (!id || id >= MAX_QUERIES) continue;
        if (active_query == id) active_query = 0;
        queries[id].used = 0;
    }
}
GLboolean glIsQuery(GLuint id) { return id && id < MAX_QUERIES && queries[id].used; }
void glBeginQuery(GLenum target, GLuint id)
{
    if (target != GL_SAMPLES_PASSED) { set_error(GL_INVALID_ENUM); return; }
    if (!id || id >= MAX_QUERIES || active_query) { set_error(GL_INVALID_OPERATION); return; }
    queries[id].used = 1; queries[id].result = 0; queries[id].active = 1;
    active_query = id;
}
void glEndQuery(GLenum target)
{
    if (target != GL_SAMPLES_PASSED) { set_error(GL_INVALID_ENUM); return; }
    if (!active_query) { set_error(GL_INVALID_OPERATION); return; }
    queries[active_query].active = 0;
    active_query = 0;
}
void glGetQueryiv(GLenum target, GLenum name, GLint *value)
{
    if (target != GL_SAMPLES_PASSED) { set_error(GL_INVALID_ENUM); return; }
    if (name == GL_QUERY_COUNTER_BITS) *value = 32;
    else if (name == GL_CURRENT_QUERY) *value = (GLint)active_query;
    else set_error(GL_INVALID_ENUM);
}
void glGetQueryObjectuiv(GLuint id, GLenum name, GLuint *value)
{
    if (!id || id >= MAX_QUERIES || !queries[id].used || queries[id].active) { set_error(GL_INVALID_OPERATION); return; }
    gl_raster_flush();   /* every queued triangle has been counted afterwards */
    if (name == GL_QUERY_RESULT) *value = queries[id].result;
    else if (name == GL_QUERY_RESULT_AVAILABLE) *value = 1;
    else set_error(GL_INVALID_ENUM);
}
void glGetQueryObjectiv(GLuint id, GLenum name, GLint *value) { GLuint v = 0; glGetQueryObjectuiv(id, name, &v); *value = (GLint)v; }
void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const unsigned char *p = pointer;
    int tex = 0, color = 0, color_ub = 0, normal = 0, offset = 0;
    switch (format) {
    case GL_V2F: case GL_V3F: break;
    case GL_C4UB_V3F: color = 4; color_ub = 1; break;
    case GL_C3F_V3F: color = 3; break;
    case GL_N3F_V3F: normal = 1; break;
    case GL_T2F_V3F: tex = 2; break;
    case GL_T2F_C3F_V3F: tex = 2; color = 3; break;
    case GL_T2F_N3F_V3F: tex = 2; normal = 1; break;
    default: set_error(GL_INVALID_ENUM); return;
    }
    int vsize = format == GL_V2F ? 2 : 3;
    int total = tex * 4 + (color_ub ? 4 : color * 4) + normal * 12 + vsize * 4;
    if (!stride) stride = total;
    va_tex[client_unit].enabled = tex != 0; va_color.enabled = color != 0; va_normal.enabled = normal; va_vertex.enabled = 1;
    if (tex) { set_array(&va_tex[client_unit], tex, GL_FLOAT, stride, p); offset += tex * 4; }
    if (color) { set_array(&va_color, color, color_ub ? GL_UNSIGNED_BYTE : GL_FLOAT, stride, p + offset); offset += color_ub ? 4 : color * 4; }
    if (normal) { set_array(&va_normal, 3, GL_FLOAT, stride, p + offset); offset += 12; }
    set_array(&va_vertex, vsize, GL_FLOAT, stride, p + offset);
}

/* -------------------------------------------------------- display lists */
GLuint glGenLists(GLsizei range)
{
    if (range <= 0) return 0;
    /* The lowest free gap: after name 0 or after a reserved range. */
    unsigned int best = 0;
    for (int c = -1; c < list_range_count; c++) {
        unsigned int base = c < 0 ? 1 : list_ranges[c].base + list_ranges[c].count;
        if (base + (unsigned int)range > MAX_LISTS || (best && base >= best)) continue;
        int clear = 1;
        for (int i = 0; i < list_range_count && clear; i++)
            if (base < list_ranges[i].base + list_ranges[i].count && list_ranges[i].base < base + (unsigned int)range) clear = 0;
        if (clear) best = base;
    }
    if (!best || list_range_count == MAX_LIST_RANGES) { set_error(GL_OUT_OF_MEMORY); return 0; }
    list_ranges[list_range_count].base = best;
    list_ranges[list_range_count++].count = (unsigned int)range;
    return best;
}
static void delete_list(unsigned int name)
{
    GlList *l = list_get(name);
    if (!l) return;
    bxgl_free(l->words);
    bxgl_free(l);
    list_pages[name / LIST_PAGE][name % LIST_PAGE] = 0;
}
void glNewList(GLuint name, GLenum mode)
{
    if (compiling || prim_mode >= 0) { set_error(GL_INVALID_OPERATION); return; }
    if (!name || name >= MAX_LISTS) { set_error(GL_INVALID_VALUE); return; }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) { set_error(GL_INVALID_ENUM); return; }
    GlList *l = bxgl_alloc(sizeof(GlList));
    if (!l) { set_error(GL_OUT_OF_MEMORY); return; }
    l->words = list_scratch; l->cap = list_scratch_cap;
    compiling = l; compiling_name = name; compile_mode = (int)mode;
}
void glEndList(void)
{
    if (!compiling) { set_error(GL_INVALID_OPERATION); return; }
    GlList *l = compiling;
    compiling = 0;
    delete_list(compiling_name);   /* first, so its memory can take the new copy */
    float *words = l->count ? bxgl_alloc(l->count * 4) : 0;
    GlList ***page = &list_pages[compiling_name / LIST_PAGE];
    if (!*page) *page = bxgl_alloc(LIST_PAGE * sizeof(GlList *));
    if ((l->count && !words) || !*page) { bxgl_free(words); bxgl_free(l); set_error(GL_OUT_OF_MEMORY); return; }
    copy_words(words, l->words, l->count);
    l->words = words; l->cap = l->count;
    (*page)[compiling_name % LIST_PAGE] = l;
}
void glDeleteLists(GLuint name, GLsizei range)
{
    if (range <= 0) return;
    unsigned int end = name + (unsigned int)range;
    for (unsigned int n = name; n < end && n < MAX_LISTS; n++) {
        if (!list_pages[n / LIST_PAGE]) { n |= LIST_PAGE - 1; continue; }
        delete_list(n);
    }
    /* Free the names: trim or split every overlapping range. */
    for (int i = 0; i < list_range_count; i++) {
        unsigned int b = list_ranges[i].base, e = b + list_ranges[i].count;
        if (e <= name || b >= end) continue;
        if (b < name && e > end && list_range_count < MAX_LIST_RANGES) {
            list_ranges[list_range_count].base = end;
            list_ranges[list_range_count++].count = e - end;
        }
        if (b < name) list_ranges[i].count = name - b;
        else if (e > end) { list_ranges[i].base = end; list_ranges[i].count = e - end; }
        else { list_ranges[i--] = list_ranges[--list_range_count]; }
    }
}
GLboolean glIsList(GLuint name) { return list_get(name) != 0; }
void glListBase(GLuint base) { list_base = base; }
static void push_attrib_impl(void)
{
    if (attrib_depth == 8) { set_error(GL_STACK_OVERFLOW); return; }
    attrib_stack[attrib_depth++] = S;
}
static void pop_attrib_impl(void)
{
    if (!attrib_depth) { set_error(GL_STACK_UNDERFLOW); return; }
    int mode = S.matrix_mode;
    S = attrib_stack[--attrib_depth];
    if (prim_mode >= 0) S.matrix_mode = mode;
}
void glPushAttrib(GLbitfield mask) { if (REC(OP_PUSH_ATTRIB, 1, (float)mask)) return; push_attrib_impl(); }
void glPopAttrib(void) { if (REC0(OP_POP_ATTRIB)) return; pop_attrib_impl(); }
void glCallList(GLuint name) { if (REC(OP_CALL_LIST, 1, (float)name)) return; execute_list(name); }
void glCallLists(GLsizei n, GLenum type, const GLvoid *names)
{
    for (int i = 0; i < n; i++) {
        unsigned int name;
        switch (type) {
        case GL_BYTE: name = (unsigned int)((const signed char *)names)[i]; break;
        case GL_UNSIGNED_BYTE: name = ((const unsigned char *)names)[i]; break;
        case GL_SHORT: name = (unsigned int)((const short *)names)[i]; break;
        case GL_UNSIGNED_SHORT: name = ((const unsigned short *)names)[i]; break;
        case GL_INT: case GL_UNSIGNED_INT: name = ((const unsigned int *)names)[i]; break;
        case GL_FLOAT: name = (unsigned int)((const float *)names)[i]; break;
        default: set_error(GL_INVALID_ENUM); return;
        }
        glCallList(list_base + name);
    }
}
static void execute_list(unsigned int name)
{
    const GlList *l = list_get(name);
    if (!l || call_depth >= 64) return;
    call_depth++;
    for (unsigned int i = 0; i < l->count;) {
        int header = (int)l->words[i], op = header / 32, n = header % 32;
        const float *a = &l->words[i + 1];
        i += 1 + (unsigned int)n;
        GlVertex v;
        switch (op) {
        case OP_BEGIN: begin_impl((GLenum)a[0]); break;
        case OP_END: end_impl(); break;
        case OP_VERTEX: if (prim_mode >= 0) { make_vertex(&v, a[0], a[1], a[2], a[3]); assemble(&v); } break;
        case OP_COLOR: copy4(S.color, a[0], a[1], a[2], a[3]); break;
        case OP_NORMAL: S.normal[0] = a[0]; S.normal[1] = a[1]; S.normal[2] = a[2]; break;
        case OP_TEXCOORD: copy4(S.tex[0], a[0], a[1], a[2], a[3]); break;
        case OP_MULTI_TEXCOORD: copy4(S.tex[(int)a[0] & 1], a[1], a[2], a[3], a[4]); break;
        case OP_ACTIVE_TEXTURE: active_texture_impl((GLenum)a[0]); break;
        case OP_BLEND_EQUATION: S.blend_eq = (int)a[0]; break;
        case OP_BLEND_COLOR: copy4(S.blend_color, a[0], a[1], a[2], a[3]); break;
        case OP_ARRAYS: {
            const unsigned int *u = (const unsigned int *)a;
            unsigned int mode = u[0], count = u[1], layout = u[2];
            int words = layout_words(layout);
            const float *data = a + 3;
            begin_impl((GLenum)mode);
            for (unsigned int k = 0; k < count; k++) unpack_vertex(layout, data + k * (unsigned int)words);
            end_impl();
            i += count * (unsigned int)words;
            break;
        }
        case OP_MATRIX_MODE: matrix_mode_impl((GLenum)a[0]); break;
        case OP_LOAD_IDENTITY: identity(current_matrix()); matrix_changed(); break;
        case OP_LOAD_MATRIX: copy16(current_matrix(), a); matrix_changed(); break;
        case OP_MULT_MATRIX: mult_impl(a); break;
        case OP_TRANSLATE: translate_impl(a[0], a[1], a[2]); break;
        case OP_ROTATE: rotate_impl(a[0], a[1], a[2], a[3]); break;
        case OP_SCALE: scale_impl(a[0], a[1], a[2]); break;
        case OP_PUSH: push_impl(); break;
        case OP_POP: pop_impl(); break;
        case OP_ORTHO: ortho_impl(a[0], a[1], a[2], a[3], a[4], a[5]); break;
        case OP_FRUSTUM: frustum_impl(a[0], a[1], a[2], a[3], a[4], a[5]); break;
        case OP_ENABLE: enable_impl((GLenum)a[0], 1); break;
        case OP_DISABLE: enable_impl((GLenum)a[0], 0); break;
        case OP_BIND_TEXTURE: bind_texture_impl((GLenum)a[0], (unsigned int)a[1]); break;
        case OP_SHADE_MODEL: S.shade_model = (int)a[0]; break;
        case OP_BLEND_FUNC: S.blend_src = (int)a[0]; S.blend_dst = (int)a[1]; break;
        case OP_DEPTH_FUNC: S.depth_func = (int)a[0]; break;
        case OP_DEPTH_MASK: S.depth_mask = a[0] != 0; break;
        case OP_CULL_FACE: S.cull_face = (int)a[0]; break;
        case OP_FRONT_FACE: S.front_face = (int)a[0]; break;
        case OP_ALPHA_FUNC: S.alpha_func = (int)a[0]; S.alpha_ref = a[1]; break;
        case OP_LINE_WIDTH: S.line_width = a[0]; break;
        case OP_POINT_SIZE: S.point_size = a[0]; break;
        case OP_POLYGON_MODE:
            if (a[0] != GL_BACK) S.poly_mode[0] = (int)a[1];
            if (a[0] != GL_FRONT) S.poly_mode[1] = (int)a[1];
            break;
        case OP_CALL_LIST: execute_list((unsigned int)a[0]); break;
        case OP_MATERIAL: material_impl((GLenum)a[0], (GLenum)a[1], a + 2); break;
        case OP_LIGHT: light_impl((GLenum)a[0], (GLenum)a[1], a + 2); break;
        case OP_LIGHT_MODEL: light_model_impl((GLenum)a[0], a + 1); break;
        case OP_COLOR_MATERIAL: S.cm_face = (int)a[0]; S.cm_mode = (int)a[1]; break;
        case OP_FOG: fog_impl((GLenum)a[0], a + 1); break;
        case OP_TEX_ENV: tex_env_impl((GLenum)a[0], a + 1); break;
        case OP_CLEAR: clear_impl((GLbitfield)a[0]); break;
        case OP_CLEAR_COLOR: copy4(S.clear_color, a[0], a[1], a[2], a[3]); break;
        case OP_PUSH_ATTRIB: push_attrib_impl(); break;
        case OP_POP_ATTRIB: pop_attrib_impl(); break;
        case OP_COLOR_MASK: for (int k = 0; k < 4; k++) S.color_mask[k] = a[k] != 0; break;
        case OP_VIEWPORT: viewport_impl((int)a[0], (int)a[1], (int)a[2], (int)a[3]); break;
        case OP_SCISSOR: for (int k = 0; k < 4; k++) S.scissor[k] = (int)a[k]; break;
        case OP_POLYGON_OFFSET: S.offset_factor = a[0]; S.offset_units = a[1]; break;
        case OP_TEX_PARAMETER: tex_parameter_impl((GLenum)a[0], (GLenum)a[1], (int)a[2]); break;
        }
    }
    call_depth--;
}

/* --------------------------------------------------------------- queries */
static char renderer[64];
const GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"Barnino Systems";
    case GL_VERSION: return (const GLubyte *)"1.5 Barnix OpenGL";
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_ARB_multitexture GL_ARB_vertex_buffer_object GL_ARB_occlusion_query GL_ARB_transpose_matrix "
            "GL_ARB_texture_env_add GL_ARB_texture_mirrored_repeat GL_ARB_texture_non_power_of_two GL_EXT_bgra GL_EXT_abgr "
            "GL_EXT_texture_edge_clamp GL_SGIS_texture_edge_clamp GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract "
            "GL_EXT_blend_func_separate GL_EXT_draw_range_elements GL_EXT_multi_draw_arrays GL_SGIS_generate_mipmap "
            "GL_EXT_texture_env_add GL_EXT_packed_pixels GL_EXT_rescale_normal";
    case GL_RENDERER: {
        const char *prefix = "Barnix SoftGL SSE2, cores: ";
        int i = 0;
        while (prefix[i]) { renderer[i] = prefix[i]; i++; }
        unsigned int cpus = bgx_cpus();
        char digits[12]; int n = 0;
        do { digits[n++] = (char)('0' + cpus % 10); cpus /= 10; } while (cpus);
        while (n) renderer[i++] = digits[--n];
        renderer[i] = 0;
        return (const GLubyte *)renderer;
    }
    default: set_error(GL_INVALID_ENUM); return 0;
    }
}
/* Returns the number of values written. */
static int get_values(GLenum name, float *out)
{
    switch (name) {
    case GL_MODELVIEW_MATRIX: copy16(out, mv_stack[mv_top]); return 16;
    case GL_PROJECTION_MATRIX: copy16(out, proj_stack[proj_top]); return 16;
    case GL_TEXTURE_MATRIX: copy16(out, tex_stack[S.active_unit][tex_top[S.active_unit]]); return 16;
    case GL_TRANSPOSE_MODELVIEW_MATRIX: transpose(out, mv_stack[mv_top]); return 16;
    case GL_TRANSPOSE_PROJECTION_MATRIX: transpose(out, proj_stack[proj_top]); return 16;
    case GL_TRANSPOSE_TEXTURE_MATRIX: transpose(out, tex_stack[S.active_unit][tex_top[S.active_unit]]); return 16;
    case GL_MAX_TEXTURE_UNITS: out[0] = UNITS; return 1;
    case GL_ACTIVE_TEXTURE: out[0] = (float)(GL_TEXTURE0 + S.active_unit); return 1;
    case GL_CLIENT_ACTIVE_TEXTURE: out[0] = (float)(GL_TEXTURE0 + client_unit); return 1;
    case GL_ARRAY_BUFFER_BINDING: out[0] = (float)array_buffer; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: out[0] = (float)element_buffer; return 1;
    case GL_BLEND_EQUATION: out[0] = (float)S.blend_eq; return 1;
    case GL_BLEND_COLOR: copy4(out, S.blend_color[0], S.blend_color[1], S.blend_color[2], S.blend_color[3]); return 4;
    case GL_BLEND_SRC_RGB: case GL_BLEND_SRC_ALPHA: out[0] = (float)S.blend_src; return 1;
    case GL_BLEND_DST_RGB: case GL_BLEND_DST_ALPHA: out[0] = (float)S.blend_dst; return 1;
    case GL_MAX_ELEMENTS_VERTICES: case GL_MAX_ELEMENTS_INDICES: out[0] = 1 << 20; return 1;
    case GL_TEXTURE_STACK_DEPTH: out[0] = (float)(tex_top[S.active_unit] + 1); return 1;
    case GL_MAX_TEXTURE_STACK_DEPTH: out[0] = 8; return 1;
    case GL_VIEWPORT: for (int k = 0; k < 4; k++) out[k] = (float)S.viewport[k]; return 4;
    case GL_SCISSOR_BOX: for (int k = 0; k < 4; k++) out[k] = (float)S.scissor[k]; return 4;
    case GL_CURRENT_COLOR: copy4(out, S.color[0], S.color[1], S.color[2], S.color[3]); return 4;
    case GL_CURRENT_TEXTURE_COORDS: { const float *t = S.tex[S.active_unit]; copy4(out, t[0], t[1], t[2], t[3]); return 4; }
    case GL_CURRENT_NORMAL: out[0] = S.normal[0]; out[1] = S.normal[1]; out[2] = S.normal[2]; return 3;
    case GL_COLOR_CLEAR_VALUE: copy4(out, S.clear_color[0], S.clear_color[1], S.clear_color[2], S.clear_color[3]); return 4;
    case GL_FOG_COLOR: copy4(out, S.fog_color[0], S.fog_color[1], S.fog_color[2], S.fog_color[3]); return 4;
    case GL_LIGHT_MODEL_AMBIENT: copy4(out, S.model_ambient[0], S.model_ambient[1], S.model_ambient[2], S.model_ambient[3]); return 4;
    case GL_COLOR_WRITEMASK: for (int k = 0; k < 4; k++) out[k] = (float)S.color_mask[k]; return 4;
    case GL_DEPTH_RANGE: out[0] = S.depth_near; out[1] = S.depth_far; return 2;
    case GL_POLYGON_MODE: out[0] = (float)S.poly_mode[0]; out[1] = (float)S.poly_mode[1]; return 2;
    case GL_MAX_VIEWPORT_DIMS: out[0] = out[1] = 4096; return 2;
    case GL_MATRIX_MODE: out[0] = (float)S.matrix_mode; return 1;
    case GL_MODELVIEW_STACK_DEPTH: out[0] = (float)(mv_top + 1); return 1;
    case GL_PROJECTION_STACK_DEPTH: out[0] = (float)(proj_top + 1); return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: out[0] = 32; return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH: out[0] = 8; return 1;
    case GL_MAX_TEXTURE_SIZE: out[0] = 4096; return 1;
    case GL_MAX_LIGHTS: out[0] = 8; return 1;
    case GL_MAX_LIST_NESTING: out[0] = 64; return 1;
    case GL_POINT_SIZE: out[0] = S.point_size; return 1;
    case GL_LINE_WIDTH: out[0] = S.line_width; return 1;
    case GL_CULL_FACE_MODE: out[0] = (float)S.cull_face; return 1;
    case GL_FRONT_FACE: out[0] = (float)S.front_face; return 1;
    case GL_SHADE_MODEL: out[0] = (float)S.shade_model; return 1;
    case GL_DEPTH_WRITEMASK: out[0] = (float)S.depth_mask; return 1;
    case GL_DEPTH_CLEAR_VALUE: out[0] = S.clear_depth; return 1;
    case GL_DEPTH_FUNC: out[0] = (float)S.depth_func; return 1;
    case GL_ALPHA_TEST_FUNC: out[0] = (float)S.alpha_func; return 1;
    case GL_ALPHA_TEST_REF: out[0] = S.alpha_ref; return 1;
    case GL_BLEND_SRC: out[0] = (float)S.blend_src; return 1;
    case GL_BLEND_DST: out[0] = (float)S.blend_dst; return 1;
    case GL_TEXTURE_BINDING_2D: out[0] = (float)S.bound[S.active_unit]; return 1;
    case GL_LIST_INDEX: out[0] = compiling ? (float)compiling_name : 0; return 1;
    case GL_LIST_MODE: out[0] = compiling ? (float)compile_mode : 0; return 1;
    case GL_FOG_MODE: out[0] = (float)S.fog_mode; return 1;
    case GL_FOG_DENSITY: out[0] = S.fog_density; return 1;
    case GL_FOG_START: out[0] = S.fog_start; return 1;
    case GL_FOG_END: out[0] = S.fog_end; return 1;
    case GL_UNPACK_ALIGNMENT: out[0] = (float)unpack_alignment; return 1;
    case GL_PACK_ALIGNMENT: out[0] = (float)pack_alignment; return 1;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: out[0] = 8; return 1;
    case GL_ALPHA_BITS: case GL_STENCIL_BITS: out[0] = 0; return 1;
    case GL_DEPTH_BITS: out[0] = 24; return 1;
    }
    unsigned int bit = cap_bit(name);
    if (bit != ~0U || name == GL_VERTEX_ARRAY || name == GL_COLOR_ARRAY || name == GL_NORMAL_ARRAY || name == GL_TEXTURE_COORD_ARRAY) {
        out[0] = glIsEnabled(name); return 1;
    }
    set_error(GL_INVALID_ENUM);
    return 0;
}
void glGetFloatv(GLenum name, GLfloat *out) { float v[16]; int n = get_values(name, v); for (int i = 0; i < n; i++) out[i] = v[i]; }
void glGetDoublev(GLenum name, GLdouble *out) { float v[16]; int n = get_values(name, v); for (int i = 0; i < n; i++) out[i] = v[i]; }
void glGetIntegerv(GLenum name, GLint *out) { float v[16]; int n = get_values(name, v); for (int i = 0; i < n; i++) out[i] = gl_iround(v[i]); }
void glGetBooleanv(GLenum name, GLboolean *out) { float v[16]; int n = get_values(name, v); for (int i = 0; i < n; i++) out[i] = v[i] != 0; }

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    int n = components(format);
    if (!n || type != GL_UNSIGNED_BYTE) { set_error(GL_INVALID_ENUM); return; }
    gl_raster_flush();
    int row = (width * n + pack_alignment - 1) / pack_alignment * pack_alignment;
    for (int j = 0; j < height; j++) {
        unsigned char *out = (unsigned char *)pixels + j * row;
        int cy = gl_frame.height - 1 - (y + j);
        for (int i = 0; i < width; i++, out += n) {
            int cx = x + i;
            unsigned int v = cx >= 0 && cy >= 0 && cx < gl_frame.width && cy < gl_frame.height ? gl_frame.color[cy * gl_frame.width + cx] : 0;
            unsigned char r = (unsigned char)(v >> 16), g = (unsigned char)(v >> 8), b = (unsigned char)v;
            switch (format) {
            case GL_BGR: case GL_BGRA: out[0] = b; out[1] = g; out[2] = r; if (n == 4) out[3] = 255; break;
            case GL_LUMINANCE: out[0] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8); break;
            case GL_ALPHA: out[0] = 255; break;
            default: out[0] = r; if (n > 1) out[1] = g; if (n > 2) out[2] = b; if (n > 3) out[3] = 255;
            }
        }
    }
}

/* =============================================================== context */
static void reset_state(void)
{
    zero(&S, sizeof(S));
    copy4(S.color, 1, 1, 1, 1);
    S.normal[2] = 1; S.tex[0][3] = 1;
    S.clear_depth = 1;
    S.depth_func = GL_LESS; S.depth_mask = 1;
    S.blend_src = GL_ONE; S.blend_dst = GL_ZERO;
    S.alpha_func = GL_ALWAYS;
    S.cull_face = GL_BACK; S.front_face = GL_CCW;
    S.poly_mode[0] = S.poly_mode[1] = GL_FILL; S.shade_model = GL_SMOOTH;
    S.point_size = S.line_width = 1;
    S.scissor[2] = S.viewport[2] = gl_frame.width; S.scissor[3] = S.viewport[3] = gl_frame.height;
    S.depth_near = 0; S.depth_far = 1;
    for (int k = 0; k < 4; k++) S.color_mask[k] = 1;
    for (int i = 0; i < 8; i++) {
        GlLight *l = &S.light[i];
        copy4(l->ambient, 0, 0, 0, 1);
        if (i == 0) { copy4(l->diffuse, 1, 1, 1, 1); copy4(l->specular, 1, 1, 1, 1); }
        else { copy4(l->diffuse, 0, 0, 0, 1); copy4(l->specular, 0, 0, 0, 1); }
        copy4(l->position, 0, 0, 1, 0);
        l->dir[2] = 1;
        l->spot[2] = l->spot_unit[2] = -1;
        l->spot_cutoff = 180; l->spot_cos = -1;
        l->attenuation[0] = 1;
    }
    for (int side = 0; side < 2; side++) {
        GlMaterial *m = &S.material[side];
        copy4(m->ambient, 0.2f, 0.2f, 0.2f, 1); copy4(m->diffuse, 0.8f, 0.8f, 0.8f, 1);
        copy4(m->specular, 0, 0, 0, 1); copy4(m->emission, 0, 0, 0, 1);
    }
    copy4(S.model_ambient, 0.2f, 0.2f, 0.2f, 1);
    S.cm_face = GL_FRONT_AND_BACK; S.cm_mode = GL_AMBIENT_AND_DIFFUSE;
    S.fog_mode = GL_EXP; S.fog_density = 1; S.fog_end = 1;
    S.env_mode[0] = S.env_mode[1] = GL_MODULATE;
    S.tex[1][3] = 1;
    S.blend_eq = GL_FUNC_ADD;
    S.matrix_mode = GL_MODELVIEW;
    mv_top = proj_top = 0;
    identity(mv_stack[0]); identity(proj_stack[0]);
    for (int u = 0; u < UNITS; u++) { tex_top[u] = 0; identity(tex_stack[u][0]); tex_identity[u] = 1; }
    matrices_dirty = 1;
    attrib_depth = 0; error_flag = 0; prim_mode = -1;
    zero(&default_texture, sizeof(default_texture));
    default_texture.min_filter = GL_NEAREST_MIPMAP_LINEAR; default_texture.mag_filter = GL_LINEAR;
    default_texture.wrap_s = default_texture.wrap_t = GL_REPEAT;
    for (int i = 0; i < MAX_TEXTURES; i++) { textures[i] = 0; texture_names[i] = 0; }
    for (unsigned int i = 0; i < MAX_LISTS / LIST_PAGE; i++) list_pages[i] = 0;
    list_range_count = 0;
    compiling = 0; call_depth = 0; list_base = 0;
    list_scratch = 0; list_scratch_cap = 0;
    zero(&va_vertex, sizeof(GlArray)); zero(&va_color, sizeof(GlArray));
    zero(&va_normal, sizeof(GlArray)); zero(va_tex, sizeof(va_tex));
    client_unit = 0;
    for (int i = 0; i < MAX_BUFFERS; i++) { buffers[i] = 0; buffer_names[i] = 0; }
    array_buffer = element_buffer = 0;
    zero(queries, sizeof(queries));
    active_query = 0;
    unpack_alignment = pack_alignment = 4; unpack_row_length = 0;
}
int bxgl_context(unsigned int *color, float *depth, int width, int height, void *heap, unsigned int heap_bytes)
{
    if (!color || !depth || width <= 0 || height <= 0 || width > 4096 || height > 4096 || heap_bytes < (256U << 10)) return -1;
    gl_fpu_enable();
    heap_init(heap, heap_bytes);
    GlFrame *f = &gl_frame;
    f->color = color; f->depth = depth; f->width = width; f->height = height;
    f->state_cap = 256;
    f->states = bxgl_alloc(f->state_cap * sizeof(GlRaster));
    f->tri_cap = heap_bytes / 4 / sizeof(GlTri);
    if (f->tri_cap > 16384) f->tri_cap = 16384;
    f->tris = bxgl_alloc(f->tri_cap * sizeof(GlTri));
    f->tri_count = f->state_count = 0;
    f->query_bands = (unsigned int)(height + 15) / 16;
    f->query_counts = bxgl_alloc(f->query_bands * f->state_cap * 4);
    f->query_add = query_add;
    if (!f->states || !f->tris || !f->query_counts) return -1;
    reset_state();
    canvas.pixels = color; canvas.width = width; canvas.height = height;
    for (int i = 0; i < width * height; i++) { color[i] = 0; depth[i] = 1.0f; }
    ready = 1;
    return 0;
}

/* Without extended memory: a 320x200 context inside the program arena. */
#define SMALL_W 320
#define SMALL_H 200
static unsigned int small_arena[(SMALL_W * SMALL_H * 8 + (1U << 20)) / 4];
static unsigned int frames, fps_start, fps_value;
int bxgl_open(int width, int height)
{
    gl_fpu_enable();
    int dw = 640, dh = 400;
    if (bgx_display(&dw, &dh)) { dw = 640; dh = 400; }
    if (width <= 0 || height <= 0) {
        width = dw; height = dh;
        if (width > 1024) width = 1024;
        if (height > 768) height = 768;
    }
    if (width > 4096 || height > 4096) return -1;
    unsigned int buffers = (unsigned int)width * height * 8, bytes = buffers + (48U << 20);
    unsigned char *memory = barnix->version >= 10 ? barnix->memory(&bytes) : 0;
    if (memory && bytes < buffers + (1U << 20)) memory = 0;
    if (!memory) {
        width = SMALL_W; height = SMALL_H;
        buffers = SMALL_W * SMALL_H * 8;
        memory = (unsigned char *)small_arena; bytes = sizeof(small_arena);
    }
    unsigned int *color = (unsigned int *)memory;
    float *depth = (float *)(memory + (unsigned int)width * height * 4);
    if (bxgl_context(color, depth, width, height, memory + buffers, bytes - buffers)) return -1;
    if (bgx_open(&canvas, color, width, height)) { ready = 0; return -1; }
    frames = 0; fps_start = bgx_millis(); fps_value = 0;
    return 0;
}
int bxgl_width(void) { return gl_frame.width; }
int bxgl_height(void) { return gl_frame.height; }
BgxCanvas *bxgl_canvas(void) { return &canvas; }
int bxgl_swap(void)
{
    gl_raster_flush();
    frames++;
    unsigned int now = bgx_millis();
    if (now - fps_start >= 1000) { fps_value = frames * 1000 / (now - fps_start); frames = 0; fps_start = now; }
    return bgx_present(&canvas);
}
int bxgl_fps(void) { return (int)fps_value; }
void bxgl_close(void)
{
    gl_raster_flush();
    ready = 0;
    bgx_close();
}
