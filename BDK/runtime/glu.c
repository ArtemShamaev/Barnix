#include <barnix_opengl.h>
/* Barnix GLU on top of Barnix OpenGL. Quadrics follow the classic SGI GLU
 * geometry: spheres and cylinders around +z, disks in the z = 0 plane. */

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble z_near, GLdouble z_far)
{
    double f = 1.0 / tan(fovy * M_PI / 360.0), m[16] = {0};
    if (aspect == 0 || z_near == z_far) return;
    m[0] = f / aspect; m[5] = f;
    m[10] = (z_far + z_near) / (z_near - z_far); m[11] = -1;
    m[14] = 2 * z_far * z_near / (z_near - z_far);
    glMultMatrixd(m);
}
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top) { glOrtho(left, right, bottom, top, -1, 1); }
static void normalize(double *v)
{
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void cross(double *out, const double *a, const double *b)
{
    out[0] = a[1] * b[2] - a[2] * b[1]; out[1] = a[2] * b[0] - a[0] * b[2]; out[2] = a[0] * b[1] - a[1] * b[0];
}
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz)
{
    double f[3] = {cx - ex, cy - ey, cz - ez}, up[3] = {ux, uy, uz}, s[3], u[3];
    normalize(f);
    cross(s, f, up); normalize(s);
    cross(u, s, f);
    double m[16] = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1};
    glMultMatrixd(m);
    glTranslated(-ex, -ey, -ez);
}
void gluPickMatrix(GLdouble x, GLdouble y, GLdouble w, GLdouble h, const GLint viewport[4])
{
    if (w <= 0 || h <= 0) return;
    glTranslated((viewport[2] - 2 * (x - viewport[0])) / w, (viewport[3] - 2 * (y - viewport[1])) / h, 0);
    glScaled(viewport[2] / w, viewport[3] / h, 1);
}
static void mul_vector(double *out, const double *m, const double *v)
{
    for (int i = 0; i < 4; i++) out[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i] * v[3];
}
static void mul_matrix(double *out, const double *a, const double *b)
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}
static int invert(double *out, const double *m)
{
    double a[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[c * 4 + r] : (c - 4 == r);
    for (int c = 0; c < 4; c++) {
        int pivot = c;
        for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[pivot][c])) pivot = r;
        if (a[pivot][c] == 0) return 0;
        for (int k = 0; k < 8; k++) { double t = a[c][k]; a[c][k] = a[pivot][k]; a[pivot][k] = t; }
        double d = a[c][c];
        for (int k = 0; k < 8; k++) a[c][k] /= d;
        for (int r = 0; r < 4; r++)
            if (r != c) { double f = a[r][c]; for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k]; }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[c * 4 + r] = a[r][c + 4];
    return 1;
}
GLint gluProject(GLdouble ox, GLdouble oy, GLdouble oz, const GLdouble model[16], const GLdouble proj[16],
                 const GLint viewport[4], GLdouble *wx, GLdouble *wy, GLdouble *wz)
{
    double in[4] = {ox, oy, oz, 1}, eye[4], clip[4];
    mul_vector(eye, model, in);
    mul_vector(clip, proj, eye);
    if (clip[3] == 0) return GL_FALSE;
    for (int i = 0; i < 3; i++) clip[i] /= clip[3];
    *wx = viewport[0] + (1 + clip[0]) * viewport[2] / 2;
    *wy = viewport[1] + (1 + clip[1]) * viewport[3] / 2;
    *wz = (1 + clip[2]) / 2;
    return GL_TRUE;
}
GLint gluUnProject(GLdouble wx, GLdouble wy, GLdouble wz, const GLdouble model[16], const GLdouble proj[16],
                   const GLint viewport[4], GLdouble *ox, GLdouble *oy, GLdouble *oz)
{
    double pm[16], inv[16], out[4];
    mul_matrix(pm, proj, model);
    if (!invert(inv, pm) || !viewport[2] || !viewport[3]) return GL_FALSE;
    double in[4] = {(wx - viewport[0]) * 2 / viewport[2] - 1, (wy - viewport[1]) * 2 / viewport[3] - 1, 2 * wz - 1, 1};
    mul_vector(out, inv, in);
    if (out[3] == 0) return GL_FALSE;
    *ox = out[0] / out[3]; *oy = out[1] / out[3]; *oz = out[2] / out[3];
    return GL_TRUE;
}
const GLubyte *gluErrorString(GLenum error)
{
    switch (error) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
    case GL_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
    case GL_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    default: return (const GLubyte *)"unknown error";
    }
}
const GLubyte *gluGetString(GLenum name)
{
    if (name == GLU_VERSION) return (const GLubyte *)"1.3 Barnix";
    if (name == GLU_EXTENSIONS) return (const GLubyte *)"";
    return 0;
}

/* --------------------------------------------------------------- images */
static int channels(GLenum format)
{
    switch (format) {
    case GL_RGB: case GL_BGR: return 3;
    case GL_RGBA: case GL_BGRA: return 4;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_LUMINANCE: case GL_ALPHA: case GL_RED: case GL_GREEN: case GL_BLUE: return 1;
    default: return 0;
    }
}
/* Box-filtered scale of 8-bit images, any number of channels. */
GLint gluScaleImage(GLenum format, GLsizei wi, GLsizei hi, GLenum type_in, const void *data_in,
                    GLsizei wo, GLsizei ho, GLenum type_out, GLvoid *data_out)
{
    int n = channels(format);
    if (!n || type_in != GL_UNSIGNED_BYTE || type_out != GL_UNSIGNED_BYTE) return GLU_INVALID_ENUM;
    if (wi <= 0 || hi <= 0 || wo <= 0 || ho <= 0) return GLU_INVALID_VALUE;
    const unsigned char *in = data_in;
    unsigned char *out = data_out;
    for (int y = 0; y < ho; y++) {
        int y0 = y * hi / ho, y1 = (y + 1) * hi / ho;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < wo; x++) {
            int x0 = x * wi / wo, x1 = (x + 1) * wi / wo;
            if (x1 <= x0) x1 = x0 + 1;
            for (int c = 0; c < n; c++) {
                unsigned int sum = 0, count = 0;
                for (int j = y0; j < y1; j++)
                    for (int i = x0; i < x1; i++) { sum += in[(j * wi + i) * n + c]; count++; }
                out[(y * wo + x) * n + c] = (unsigned char)((sum + count / 2) / count);
            }
        }
    }
    return 0;
}
static int floor_pot(int v) { int p = 1; while (p * 2 <= v && p < 1024) p *= 2; return p; }
GLint gluBuild2DMipmaps(GLenum target, GLint internal, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data)
{
    int n = channels(format);
    if (!n || type != GL_UNSIGNED_BYTE) return GLU_INVALID_ENUM;
    if (width <= 0 || height <= 0) return GLU_INVALID_VALUE;
    int w = floor_pot(width), h = floor_pot(height);
    unsigned char *a = bxgl_alloc((unsigned int)(w * h * n)), *b = bxgl_alloc((unsigned int)(w * h * n));
    if (!a || !b) { bxgl_free(a); bxgl_free(b); return GLU_OUT_OF_MEMORY; }
    GLint align;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w == width && h == height && !((width * n) % align)) {
        for (int i = 0; i < w * h * n; i++) a[i] = ((const unsigned char *)data)[i];
    } else {
        /* Respect the caller's row alignment while scaling down to a power of two. */
        int row = (width * n + align - 1) / align * align;
        unsigned char *packed = bxgl_alloc((unsigned int)(width * height * n));
        if (!packed) { bxgl_free(a); bxgl_free(b); glPixelStorei(GL_UNPACK_ALIGNMENT, align); return GLU_OUT_OF_MEMORY; }
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width * n; x++) packed[y * width * n + x] = ((const unsigned char *)data)[y * row + x];
        gluScaleImage(format, width, height, GL_UNSIGNED_BYTE, packed, w, h, GL_UNSIGNED_BYTE, a);
        bxgl_free(packed);
    }
    for (int level = 0;; level++) {
        glTexImage2D(target, level, internal, w, h, 0, format, GL_UNSIGNED_BYTE, a);
        if (w == 1 && h == 1) break;
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        gluScaleImage(format, w, h, GL_UNSIGNED_BYTE, a, nw, nh, GL_UNSIGNED_BYTE, b);
        unsigned char *t = a; a = b; b = t;
        w = nw; h = nh;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    bxgl_free(a); bxgl_free(b);
    return 0;
}

/* ------------------------------------------------------------- quadrics */
struct GLUquadric { GLenum draw, normals, orientation; GLboolean texture; };
static GLUquadric quadric_pool[16];
static unsigned char quadric_used[16];
GLUquadric *gluNewQuadric(void)
{
    for (int i = 0; i < 16; i++)
        if (!quadric_used[i]) {
            quadric_used[i] = 1;
            GLUquadric *q = &quadric_pool[i];
            q->draw = GLU_FILL; q->normals = GLU_SMOOTH; q->orientation = GLU_OUTSIDE; q->texture = GL_FALSE;
            return q;
        }
    return 0;
}
void gluDeleteQuadric(GLUquadric *q) { if (q >= quadric_pool && q < quadric_pool + 16) quadric_used[q - quadric_pool] = 0; }
void gluQuadricDrawStyle(GLUquadric *q, GLenum draw) { if (q) q->draw = draw; }
void gluQuadricNormals(GLUquadric *q, GLenum normals) { if (q) q->normals = normals; }
void gluQuadricOrientation(GLUquadric *q, GLenum orientation) { if (q) q->orientation = orientation; }
void gluQuadricTexture(GLUquadric *q, GLboolean texture) { if (q) q->texture = texture; }

/* Draw styles other than fill use the polygon mode for the duration. */
static GLint saved_mode[2];
static void style_begin(const GLUquadric *q)
{
    glGetIntegerv(GL_POLYGON_MODE, saved_mode);
    if (q->draw == GLU_LINE || q->draw == GLU_SILHOUETTE) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else if (q->draw == GLU_POINT) glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
}
static void style_end(const GLUquadric *q)
{
    if (q->draw == GLU_FILL) return;
    glPolygonMode(GL_FRONT, (GLenum)saved_mode[0]);
    glPolygonMode(GL_BACK, (GLenum)saved_mode[1]);
}
static void emit(const GLUquadric *q, double nx, double ny, double nz, double s, double t, double x, double y, double z)
{
    if (q->normals != GLU_NONE) {
        if (q->orientation == GLU_INSIDE) glNormal3d(-nx, -ny, -nz); else glNormal3d(nx, ny, nz);
    }
    if (q->texture) glTexCoord2d(s, t);
    glVertex3d(x, y, z);
}
void gluSphere(GLUquadric *q, GLdouble radius, GLint slices, GLint stacks)
{
    if (!q || slices < 2 || stacks < 1) return;
    style_begin(q);
    for (int i = 0; i < stacks; i++) {
        double rho0 = M_PI * i / stacks, rho1 = M_PI * (i + 1) / stacks;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            double theta = j == slices ? 0 : 2 * M_PI * j / slices, st = sin(theta), ct = cos(theta);
            double rho[2] = {rho0, rho1};
            for (int k = 0; k < 2; k++) {
                double r = q->orientation == GLU_INSIDE ? rho[1 - k] : rho[k];
                double x = -st * sin(r), y = ct * sin(r), z = cos(r);
                emit(q, x, y, z, 1.0 - (double)j / slices, 1.0 - r / M_PI, x * radius, y * radius, z * radius);
            }
        }
        glEnd();
    }
    style_end(q);
}
void gluCylinder(GLUquadric *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    if (!q || slices < 2 || stacks < 1 || height == 0) return;
    double slope = (base - top) / height, len = sqrt(1 + slope * slope), nz = slope / len, nxy = 1 / len;
    style_begin(q);
    for (int i = 0; i < stacks; i++) {
        double z[2] = {height * i / stacks, height * (i + 1) / stacks};
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            double theta = j == slices ? 0 : 2 * M_PI * j / slices, st = sin(theta), ct = cos(theta);
            for (int k = 0; k < 2; k++) {
                double zz = q->orientation == GLU_INSIDE ? z[1 - k] : z[k], r = base + (top - base) * zz / height;
                emit(q, st * nxy, ct * nxy, nz, (double)j / slices, zz / height, st * r, ct * r, zz);
            }
        }
        glEnd();
    }
    style_end(q);
}
void gluPartialDisk(GLUquadric *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops, GLdouble start, GLdouble sweep)
{
    if (!q || slices < 2 || loops < 1 || outer <= 0 || inner < 0 || inner > outer) return;
    if (sweep > 360) sweep = 360;
    if (sweep < -360) sweep = -360;
    style_begin(q);
    for (int i = 0; i < loops; i++) {
        double r[2] = {outer - (outer - inner) * i / loops, outer - (outer - inner) * (i + 1) / loops};
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            double a = (start + sweep * j / slices) * M_PI / 180.0, sa = sin(a), ca = cos(a);
            for (int k = 0; k < 2; k++) {
                double rr = q->orientation == GLU_INSIDE ? r[1 - k] : r[k];
                emit(q, 0, 0, 1, 0.5 + sa * rr / (2 * outer), 0.5 + ca * rr / (2 * outer), sa * rr, ca * rr, 0);
            }
        }
        glEnd();
    }
    style_end(q);
}
void gluDisk(GLUquadric *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    gluPartialDisk(q, inner, outer, slices, loops, 0, 360);
}
