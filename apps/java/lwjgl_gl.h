/* Helpers for the generated GL bindings (natives_gl.c). */
#ifndef BJVM_LWJGL_GL_H
#define BJVM_LWJGL_GL_H
#include <GL/gl.h>

/* Element size of a java.nio buffer object. */
static inline uint32_t buf_element(Object *b)
{
    const char *n = b->cls->name;
    if (!strncmp(n, "java/nio/", 9)) {
        switch (n[9]) {
        case 'B': return 1;
        case 'S': case 'C': return 2;
        case 'L': case 'D': return 8;
        default: return 4;
        }
    }
    return 1;
}
/* Address of the buffer's current position (LWJGL semantics). */
static inline void *bufptr(Thread *t, Object *b)
{
    (void)t;
    if (!b) return 0;
    Array *hb = FIELD(b, vm.buffer_array, Array *);
    if (!hb) return 0;
    return ADATA(hb, uint8_t) + FIELD(b, vm.buffer_offset, jint) + FIELD(b, vm.buffer_position, jint) * (jint)buf_element(b);
}
static inline jint bufrem(Object *b) { return b ? FIELD(b, vm.buffer_limit, jint) - FIELD(b, vm.buffer_position, jint) : 0; }
static inline GLsizeiptr bufbytes(Object *b) { return b ? (GLsizeiptr)bufrem(b) * buf_element(b) : 0; }
/* Array pointers outlive the call: keep their buffers reachable. */
void lwjgl_keep(int slot, Object *buffer);
#define keep(t, slot, o) lwjgl_keep(slot, o)
static inline void fog_iv(GLenum name, const GLint *v) { GLfloat f[4]; for (int i = 0; i < 4; i++) f[i] = name == GL_FOG_COLOR ? v[i] / 2147483647.0f : (GLfloat)v[i]; glFogfv(name, f); }
static inline void light_iv(GLenum light, GLenum name, const GLint *v) { GLfloat f[4]; for (int i = 0; i < 4; i++) f[i] = (GLfloat)v[i]; glLightfv(light, name, f); }
static inline void get_tex_image(GLenum target, GLint level, GLenum format, GLenum type, void *out) { (void)target; (void)level; (void)format; (void)type; (void)out; }
/* Minecraft Alpha quits on any GL error. Running out of OpenGL memory only
 * loses the geometry that did not fit, so it is reported once on the
 * console instead of to the program. */
static inline jint lwjgl_get_error(void)
{
    static int warned;
    GLenum e = glGetError();
    if (e != GL_OUT_OF_MEMORY) return (jint)e;
    if (!warned) { warned = 1; vm_printf(1, "bjvm: OpenGL memory is full; some geometry is not drawn\n"); }
    return 0;
}
#endif
