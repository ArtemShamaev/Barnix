#include "gl_internal.h"
/* Barnix OpenGL rasterizer. Triangles are set up on the calling core and
 * queued; a flush splits the color buffer into bands of rows and every core
 * draws the whole queue into its own bands, so primitives stay in order.
 * Vertices snap to 1/16 pixel and edge functions are evaluated exactly in
 * double precision: shared edges never crack or draw twice. */
GlFrame gl_frame;

#define BAND 16

void gl_fpu_enable(void)
{
#ifndef BXGL_HOST
    /* Applications run in ring 0: clear EM and TS, set MP and NE. */
    unsigned int cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 = (cr0 & ~12U) | 0x22U;
    __asm__ volatile("mov %0,%%cr0; fninit" :: "r"(cr0) : "memory");
#endif
}

static int same_state(const GlRaster *a, const GlRaster *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (unsigned int i = 0; i < sizeof(GlRaster); i++) if (x[i] != y[i]) return 0;
    return 1;
}
static unsigned int state_index(const GlRaster *state)
{
    GlFrame *f = &gl_frame;
    if (f->state_count && same_state(&f->states[f->state_count - 1], state)) return f->state_count - 1;
    if (f->state_count == f->state_cap) gl_raster_flush();
    unsigned char *out = (unsigned char *)&f->states[f->state_count];
    const unsigned char *in = (const unsigned char *)state;
    for (unsigned int i = 0; i < sizeof(GlRaster); i++) out[i] = in[i];
    return f->state_count++;
}

static double snap(float v) { return gl_iround(v * 16.0) * (1.0 / 16.0); }

void gl_raster_triangle(const GlScreenVertex *a, const GlScreenVertex *b, const GlScreenVertex *c, const GlRaster *rs)
{
    GlFrame *f = &gl_frame;
    double x0 = snap(a->x), y0 = snap(a->y), x1 = snap(b->x), y1 = snap(b->y), x2 = snap(c->x), y2 = snap(c->y);
    double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0 || area != area) return;
    if (area < 0) {   /* make the inside positive on every edge */
        const GlScreenVertex *sv = b; b = c; c = sv;
        double t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t;
        area = -area;
    }
    double lo_x = x0 < x1 ? x0 : x1, hi_x = x0 > x1 ? x0 : x1, lo_y = y0 < y1 ? y0 : y1, hi_y = y0 > y1 ? y0 : y1;
    if (x2 < lo_x) lo_x = x2;
    if (x2 > hi_x) hi_x = x2;
    if (y2 < lo_y) lo_y = y2;
    if (y2 > hi_y) hi_y = y2;
    int minx = gl_ifloor(lo_x - 0.5) + 1, maxx = gl_ifloor(hi_x - 0.5), miny = gl_ifloor(lo_y - 0.5) + 1, maxy = gl_ifloor(hi_y - 0.5);
    if (lo_x - 0.5 == minx - 1) minx--;          /* a center exactly on the box edge may be inside */
    if (lo_y - 0.5 == miny - 1) miny--;
    if (minx < rs->x0) minx = rs->x0;
    if (miny < rs->y0) miny = rs->y0;
    if (maxx > rs->x1 - 1) maxx = rs->x1 - 1;
    if (maxy > rs->y1 - 1) maxy = rs->y1 - 1;
    if (minx > maxx || miny > maxy) return;

    if (f->tri_count == f->tri_cap) gl_raster_flush();
    unsigned int state = state_index(rs);
    GlTri *t = &f->tris[f->tri_count++];
    const double xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    t->tie = 0;
    for (int k = 0; k < 3; k++) {
        int j = k == 2 ? 0 : k + 1;
        t->ea[k] = ys[k] - ys[j];
        t->eb[k] = xs[j] - xs[k];
        t->ec[k] = xs[k] * ys[j] - xs[j] * ys[k];
        if (t->ea[k] > 0 || (t->ea[k] == 0 && t->eb[k] > 0)) t->tie |= (unsigned char)(1U << k);
    }
    t->minx = (short)minx; t->maxx = (short)maxx; t->miny = (short)miny; t->maxy = (short)maxy;
    t->state = (unsigned short)state;
    t->x0 = (float)x0; t->y0 = (float)y0;

    double dx1 = x1 - x0, dy1 = y1 - y0, dx2 = x2 - x0, dy2 = y2 - y0, inv = 1.0 / area;
    int textured = rs->flags & (RF_TEXTURE | RF_TEXTURE1);
    double va[PLANES][3] = {
        {a->z, b->z, c->z}, {a->r, b->r, c->r}, {a->g, b->g, c->g}, {a->b, b->b, c->b},
        {a->a, b->a, c->a}, {a->f, b->f, c->f}, {a->iw, b->iw, c->iw},
        {a->s * a->iw, b->s * b->iw, c->s * c->iw}, {a->t * a->iw, b->t * b->iw, c->t * c->iw},
        {a->s1 * a->iw, b->s1 * b->iw, c->s1 * c->iw}, {a->t1 * a->iw, b->t1 * b->iw, c->t1 * c->iw}};
    int planes = rs->flags & RF_TEXTURE1 ? PLANES : textured ? PS1 : PW;
    for (int k = 0; k < planes; k++) {
        double d1 = va[k][1] - va[k][0], d2 = va[k][2] - va[k][0];
        t->p[k][0] = (float)va[k][0];
        t->p[k][1] = (float)((d1 * dy2 - d2 * dy1) * inv);
        t->p[k][2] = (float)((d2 * dx1 - d1 * dx2) * inv);
    }
    if (rs->flags & RF_OFFSET) {
        float sx = t->p[PZ][1] < 0 ? -t->p[PZ][1] : t->p[PZ][1], sy = t->p[PZ][2] < 0 ? -t->p[PZ][2] : t->p[PZ][2];
        t->p[PZ][0] += rs->offset_factor * (sx > sy ? sx : sy) + rs->offset_units * (1.0f / 16777216.0f);
    }
    t->level = t->linear = t->level1 = t->linear1 = 0;
    for (int u = 0; u < GL_TEXTURE_UNITS; u++) {
        if (!(rs->flags & (u ? RF_TEXTURE1 : RF_TEXTURE))) continue;
        /* One mipmap level per triangle, from its texel to pixel area ratio. */
        const GlUnit *unit = &rs->unit[u];
        const GlLevel *base = &unit->tex->level[0];
        double sa = u ? a->s1 : a->s, ta = u ? a->t1 : a->t;
        double s1 = (u ? b->s1 : b->s) - sa, t1 = (u ? b->t1 : b->t) - ta, s2 = (u ? c->s1 : c->s) - sa, t2 = (u ? c->t1 : c->t) - ta;
        double texels = (s1 * t2 - s2 * t1) * base->w * base->h;
        if (texels < 0) texels = -texels;
        int minify = texels > area;
        int filter = minify ? unit->min_filter : unit->mag_filter;
        unsigned char linear = filter == GL_LINEAR || filter == GL_LINEAR_MIPMAP_NEAREST || filter == GL_LINEAR_MIPMAP_LINEAR;
        unsigned char level = 0;
        if (minify && filter != GL_NEAREST && filter != GL_LINEAR && unit->levels > 1) {
            double ratio = texels / area;
            while (ratio > 2.83 && level < unit->levels - 1) { ratio *= 0.25; level++; }   /* 2^1.5: rounds lod */
        }
        if (u) { t->level1 = level; t->linear1 = linear; } else { t->level = level; t->linear = linear; }
    }
}

/* ---------------------------------------------------------------- pixels */
static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int wrap(int u, int size, int mask, int mode)
{
    if (mode == GL_REPEAT) {
        if (mask >= 0) return u & mask;
        u %= size;
        return u < 0 ? u + size : u;
    }
    if (mode == GL_MIRRORED_REPEAT) {
        int period = size * 2;
        u %= period;
        if (u < 0) u += period;
        return u < size ? u : period - 1 - u;
    }
    return u < 0 ? 0 : u >= size ? size - 1 : u;
}
static unsigned int lerp_texel(unsigned int p, unsigned int q, unsigned int w)
{
    unsigned int rb = ((p & 0xff00ffU) * (256 - w) + (q & 0xff00ffU) * w) >> 8 & 0xff00ffU;
    unsigned int ag = ((p >> 8 & 0xff00ffU) * (256 - w) + (q >> 8 & 0xff00ffU) * w) & 0xff00ff00U;
    return rb | ag;
}
static unsigned int sample(const GlUnit *rs, const GlLevel *l, int linear, float s, float t)
{
    if (!linear) {
        int u = wrap(gl_ifloor(s * l->w), l->w, l->wmask, rs->wrap_s);
        int v = wrap(gl_ifloor(t * l->h), l->h, l->hmask, rs->wrap_t);
        return l->texels[v * l->w + u];
    }
    float us = s * l->w - 0.5f, vs = t * l->h - 0.5f;
    int u0 = gl_ifloor(us), v0 = gl_ifloor(vs);
    unsigned int fu = (unsigned int)gl_iround((us - u0) * 256.0f), fv = (unsigned int)gl_iround((vs - v0) * 256.0f);
    if (fu > 256) fu = 256;
    if (fv > 256) fv = 256;
    int ua = wrap(u0, l->w, l->wmask, rs->wrap_s), ub = wrap(u0 + 1, l->w, l->wmask, rs->wrap_s);
    const unsigned int *ra = l->texels + wrap(v0, l->h, l->hmask, rs->wrap_t) * l->w;
    const unsigned int *rb = l->texels + wrap(v0 + 1, l->h, l->hmask, rs->wrap_t) * l->w;
    return lerp_texel(lerp_texel(ra[ua], ra[ub], fu), lerp_texel(rb[ua], rb[ub], fu), fv);
}
static int depth_pass(int func, float z, float d)
{
    switch (func) {
    case GL_LESS: return z < d;
    case GL_LEQUAL: return z <= d;
    case GL_GREATER: return z > d;
    case GL_GEQUAL: return z >= d;
    case GL_EQUAL: return z == d;
    case GL_NOTEQUAL: return z != d;
    case GL_ALWAYS: return 1;
    default: return 0;
    }
}
static int alpha_pass(int func, int a, int ref)
{
    switch (func) {
    case GL_LESS: return a < ref;
    case GL_LEQUAL: return a <= ref;
    case GL_GREATER: return a > ref;
    case GL_GEQUAL: return a >= ref;
    case GL_EQUAL: return a == ref;
    case GL_NOTEQUAL: return a != ref;
    case GL_ALWAYS: return 1;
    default: return 0;
    }
}
/* Blend factor for one channel, 0..256. s = source RGBA, d = destination. */
static int factor(int f, const int *s, const int *d, int ch, const int *k)
{
    switch (f) {
    case GL_CONSTANT_COLOR: return k[ch] + (k[ch] >> 7);
    case GL_ONE_MINUS_CONSTANT_COLOR: return 256 - k[ch] - (k[ch] >> 7);
    case GL_CONSTANT_ALPHA: return k[3] + (k[3] >> 7);
    case GL_ONE_MINUS_CONSTANT_ALPHA: return 256 - k[3] - (k[3] >> 7);
    case GL_ZERO: return 0;
    case GL_ONE: return 256;
    case GL_SRC_COLOR: return s[ch] + (s[ch] >> 7);
    case GL_ONE_MINUS_SRC_COLOR: return 256 - s[ch] - (s[ch] >> 7);
    case GL_SRC_ALPHA: return s[3] + (s[3] >> 7);
    case GL_ONE_MINUS_SRC_ALPHA: return 256 - s[3] - (s[3] >> 7);
    case GL_DST_ALPHA: return 256;           /* the color buffer has no alpha: always 1 */
    case GL_ONE_MINUS_DST_ALPHA: return 0;
    case GL_DST_COLOR: return d[ch] + (d[ch] >> 7);
    case GL_ONE_MINUS_DST_COLOR: return 256 - d[ch] - (d[ch] >> 7);
    case GL_SRC_ALPHA_SATURATE: return ch == 3 ? 256 : 0;
    default: return 256;
    }
}

static void apply_env(const GlUnit *u, int *s, unsigned int texel)
{
    int tc[4] = {(int)(texel >> 16 & 255), (int)(texel >> 8 & 255), (int)(texel & 255), (int)(texel >> 24)};
    switch (u->env) {
    case GL_REPLACE:
        s[0] = tc[0]; s[1] = tc[1]; s[2] = tc[2];
        if (u->tex_alpha) s[3] = tc[3];
        break;
    case GL_DECAL:
        for (int k = 0; k < 3; k++) s[k] += ((tc[k] - s[k]) * (tc[3] + (tc[3] >> 7))) >> 8;
        break;
    case GL_ADD:
        for (int k = 0; k < 3; k++) s[k] = clamp255(s[k] + tc[k]);
        s[3] = (s[3] * (tc[3] + 1)) >> 8;
        break;
    case GL_BLEND:
        for (int k = 0; k < 3; k++) s[k] = (s[k] * (256 - tc[k]) + u->env_color[k] * tc[k]) >> 8;
        s[3] = (s[3] * (tc[3] + 1)) >> 8;
        break;
    default:   /* GL_MODULATE */
        for (int k = 0; k < 4; k++) s[k] = (s[k] * (tc[k] + 1)) >> 8;
    }
}
static int blend_channel(int eq, int s, int d, int fs, int fd)
{
    switch (eq) {
    case GL_FUNC_SUBTRACT: return clamp255((s * fs - d * fd) >> 8);
    case GL_FUNC_REVERSE_SUBTRACT: return clamp255((d * fd - s * fs) >> 8);
    case GL_MIN: return s < d ? s : d;
    case GL_MAX: return s > d ? s : d;
    default: return clamp255((s * fs + d * fd) >> 8);
    }
}

static void span(const GlTri *t, const GlRaster *rs, int y, int xl, int xr)
{
    GlFrame *f = &gl_frame;
    float fx = xl + 0.5f - t->x0, fy = y + 0.5f - t->y0, v[PLANES], dv[PLANES];
    unsigned int flags = rs->flags;
    int planes = flags & RF_TEXTURE1 ? PLANES : flags & RF_TEXTURE ? PS1 : PW;
    for (int k = 0; k < planes; k++) { v[k] = t->p[k][0] + t->p[k][1] * fx + t->p[k][2] * fy; dv[k] = t->p[k][1]; }
    unsigned int *cp = f->color + y * f->width, mask = rs->color_mask, passed = 0;
    float *zp = f->depth + y * f->width;
    const GlLevel *level = flags & RF_TEXTURE ? &rs->unit[0].tex->level[t->level] : 0;
    const GlLevel *level1 = flags & RF_TEXTURE1 ? &rs->unit[1].tex->level[t->level1] : 0;
    for (int x = xl; x <= xr; x++) {
        float z = v[PZ];
        if (z < 0) z = 0;
        if (z > 1) z = 1;
        if ((flags & RF_DEPTH_TEST) && !depth_pass(rs->depth_func, z, zp[x])) goto next;
        int s[4] = {clamp255(gl_iround(v[PR])), clamp255(gl_iround(v[PG])), clamp255(gl_iround(v[PB])), clamp255(gl_iround(v[PA]))};
        if (level || level1) {
            float w = 1.0f / v[PW];
            if (level) apply_env(&rs->unit[0], s, sample(&rs->unit[0], level, t->linear, v[PS] * w, v[PT] * w));
            if (level1) apply_env(&rs->unit[1], s, sample(&rs->unit[1], level1, t->linear1, v[PS1] * w, v[PT1] * w));
        }
        if (flags & RF_FOG) {
            int fog = gl_iround(v[PF] * 256.0f);
            fog = fog < 0 ? 0 : fog > 256 ? 256 : fog;
            for (int k = 0; k < 3; k++) s[k] = (s[k] * fog + rs->fog_color[k] * (256 - fog)) >> 8;
        }
        if ((flags & RF_ALPHA_TEST) && !alpha_pass(rs->alpha_func, s[3], rs->alpha_ref)) goto next;
        passed++;
        unsigned int out;
        if (flags & RF_BLEND) {
            unsigned int old = cp[x];
            int d[4] = {(int)(old >> 16 & 255), (int)(old >> 8 & 255), (int)(old & 255), 255};
            int o[3];
            for (int k = 0; k < 3; k++)
                o[k] = blend_channel(rs->blend_eq, s[k], d[k], factor(rs->blend_src, s, d, k, rs->blend_color), factor(rs->blend_dst, s, d, k, rs->blend_color));
            out = (unsigned int)o[0] << 16 | (unsigned int)o[1] << 8 | (unsigned int)o[2];
        } else
            out = (unsigned int)s[0] << 16 | (unsigned int)s[1] << 8 | (unsigned int)s[2];
        cp[x] = (cp[x] & ~mask) | (out & mask);
        if (flags & RF_DEPTH_WRITE) zp[x] = z;
    next:
        for (int k = 0; k < planes; k++) v[k] += dv[k];
    }
    if ((flags & RF_QUERY) && passed && f->query_counts)
        f->query_counts[(unsigned int)(y / BAND) * f->state_cap + t->state] += passed;
}

static int inside(double a, double k, int x, int tie)
{
    double e = a * (x + 0.5) + k;
    return e > 0 || (e == 0 && tie);
}
static void raster_rows(int r0, int r1)
{
    GlFrame *f = &gl_frame;
    for (unsigned int i = 0; i < f->tri_count; i++) {
        const GlTri *t = &f->tris[i];
        if (t->maxy < r0 || t->miny >= r1) continue;
        const GlRaster *rs = &f->states[t->state];
        int ylo = t->miny > r0 ? t->miny : r0, yhi = t->maxy < r1 - 1 ? t->maxy : r1 - 1;
        for (int y = ylo; y <= yhi; y++) {
            double yc = y + 0.5;
            int xl = t->minx, xr = t->maxx;
            for (int k = 0; k < 3 && xl <= xr; k++) {
                double a = t->ea[k], kk = t->eb[k] * yc + t->ec[k];
                int tie = t->tie >> k & 1;
                if (a == 0) {
                    if (kk < 0 || (kk == 0 && !tie)) xl = xr + 1;
                    continue;
                }
                double bound = -kk / a;
                if (bound < -100000.0) bound = -100000.0;
                if (bound > 100000.0) bound = 100000.0;
                int x = gl_ifloor(bound - 0.5);
                if (a > 0) {   /* inside to the right of the edge */
                    if (x < xl) x = xl;
                    if (x > xr + 1) x = xr + 1;
                    while (x <= xr && !inside(a, kk, x, tie)) x++;
                    while (x > xl && inside(a, kk, x - 1, tie)) x--;
                    xl = x;
                } else {
                    x++;
                    if (x > xr) x = xr;
                    if (x < xl - 1) x = xl - 1;
                    while (x >= xl && !inside(a, kk, x, tie)) x--;
                    while (x < xr && inside(a, kk, x + 1, tie)) x++;
                    xr = x;
                }
            }
            if (xl <= xr) span(t, rs, y, xl, xr);
        }
    }
}
static void raster_job(void *argument, unsigned int index)
{
    (void)argument;
    gl_fpu_enable();
    int r0 = (int)index * BAND, r1 = r0 + BAND;
    if (r1 > gl_frame.height) r1 = gl_frame.height;
    raster_rows(r0, r1);
}
void gl_raster_flush(void)
{
    GlFrame *f = &gl_frame;
    unsigned int bands = (unsigned int)(f->height + BAND - 1) / BAND;
    int queries = 0;
    for (unsigned int i = 0; i < f->state_count && !queries; i++) queries = (f->states[i].flags & RF_QUERY) != 0;
    if (queries && f->query_counts)
        for (unsigned int b = 0; b < bands && b < f->query_bands; b++)
            for (unsigned int i = 0; i < f->state_count; i++) f->query_counts[b * f->state_cap + i] = 0;
    if (f->tri_count) bgx_parallel(raster_job, 0, bands);
    if (queries && f->query_counts && f->query_add)
        for (unsigned int i = 0; i < f->state_count; i++) {
            if (!(f->states[i].flags & RF_QUERY)) continue;
            unsigned int total = 0;
            for (unsigned int b = 0; b < bands && b < f->query_bands; b++) total += f->query_counts[b * f->state_cap + i];
            f->query_add(f->states[i].query, total);
        }
    f->tri_count = 0;
    f->state_count = 0;
}

typedef struct { const GlRaster *rect; int color, depth; unsigned int value; float depth_value; } ClearJob;
static void clear_job(void *argument, unsigned int index)
{
    const ClearJob *job = argument;
    GlFrame *f = &gl_frame;
    gl_fpu_enable();
    int r0 = (int)index * BAND, r1 = r0 + BAND;
    if (r0 < job->rect->y0) r0 = job->rect->y0;
    if (r1 > job->rect->y1) r1 = job->rect->y1;
    unsigned int mask = job->rect->color_mask, value = job->value & mask;
    for (int y = r0; y < r1; y++) {
        unsigned int *c = f->color + y * f->width;
        float *d = f->depth + y * f->width;
        for (int x = job->rect->x0; x < job->rect->x1; x++) {
            if (job->color) c[x] = (c[x] & ~mask) | value;
            if (job->depth) d[x] = job->depth_value;
        }
    }
}
void gl_raster_clear(const GlRaster *rect, int color, unsigned int value, int depth, float depth_value)
{
    gl_raster_flush();
    if (rect->x0 >= rect->x1 || rect->y0 >= rect->y1) return;
    ClearJob job = {rect, color, depth, value, depth_value};
    bgx_parallel(clear_job, &job, (unsigned int)(gl_frame.height + BAND - 1) / BAND);
}
