#!/usr/bin/env python3
"""Generate the LWJGL GL classes (Java natives) and their C bindings to Barnix OpenGL.
Each spec line: <java return> <name>(<java params>) => <C statement using {0},{1}.. ; {pN} buffer pointer, {rN} remaining>"""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)

GL11 = r"""
void glAccum(int op, float value) => (void)0
void glAlphaFunc(int func, float ref) => glAlphaFunc({0}, {1})
void glArrayElement(int i) => glArrayElement({0})
void glBegin(int mode) => glBegin({0})
void glBindTexture(int target, int texture) => glBindTexture({0}, {1})
void glBlendFunc(int s, int d) => glBlendFunc({0}, {1})
void glCallList(int list) => glCallList({0})
void glCallLists(IntBuffer lists) => glCallLists({r0}, GL_UNSIGNED_INT, {p0})
void glCallLists(ByteBuffer lists) => glCallLists({r0}, GL_UNSIGNED_BYTE, {p0})
void glCallLists(ShortBuffer lists) => glCallLists({r0}, GL_UNSIGNED_SHORT, {p0})
void glClear(int mask) => glClear({0})
void glClearColor(float r, float g, float b, float a) => glClearColor({0}, {1}, {2}, {3})
void glClearDepth(double d) => glClearDepth({0})
void glClearStencil(int s) => (void)0
void glClipPlane(int plane, DoubleBuffer eq) => (void)0
void glColor3b(byte r, byte g, byte b) => glColor3ub((GLubyte)({0} & 255), (GLubyte)({1} & 255), (GLubyte)({2} & 255))
void glColor3ub(byte r, byte g, byte b) => glColor3ub((GLubyte)({0} & 255), (GLubyte)({1} & 255), (GLubyte)({2} & 255))
void glColor4b(byte r, byte g, byte b, byte a) => glColor4ub((GLubyte)({0} & 255), (GLubyte)({1} & 255), (GLubyte)({2} & 255), (GLubyte)({3} & 255))
void glColor4ub(byte r, byte g, byte b, byte a) => glColor4ub((GLubyte)({0} & 255), (GLubyte)({1} & 255), (GLubyte)({2} & 255), (GLubyte)({3} & 255))
void glColor3f(float r, float g, float b) => glColor3f({0}, {1}, {2})
void glColor3d(double r, double g, double b) => glColor3d({0}, {1}, {2})
void glColor4f(float r, float g, float b, float a) => glColor4f({0}, {1}, {2}, {3})
void glColor4d(double r, double g, double b, double a) => glColor4d({0}, {1}, {2}, {3})
void glColorMask(boolean r, boolean g, boolean b, boolean a) => glColorMask({0}, {1}, {2}, {3})
void glColorMaterial(int face, int mode) => glColorMaterial({0}, {1})
void glColorPointer(int size, int type, int stride, long offset) => glColorPointer({0}, {1}, {2}, (const void *)(uintptr_t){3})
void glColorPointer(int size, boolean unsigned, int stride, ByteBuffer p) => keep(t, 0, a[3].l), glColorPointer({0}, {1} ? GL_UNSIGNED_BYTE : GL_BYTE, {2}, {p3})
void glColorPointer(int size, int type, int stride, ByteBuffer p) => keep(t, 0, a[3].l), glColorPointer({0}, {1}, {2}, {p3})
void glColorPointer(int size, int stride, FloatBuffer p) => keep(t, 0, a[2].l), glColorPointer({0}, GL_FLOAT, {1}, {p2})
void glCopyTexImage2D(int target, int level, int internal, int x, int y, int w, int h, int border) => glCopyTexImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7})
void glCullFace(int mode) => glCullFace({0})
void glDeleteLists(int list, int range) => glDeleteLists({0}, {1})
void glDeleteTextures(IntBuffer t) => glDeleteTextures({r0}, (const GLuint *){p0})
void glDeleteTextures(int texture) => glDeleteTextures(1, (const GLuint *)&a[0].i)
void glDepthFunc(int f) => glDepthFunc({0})
void glDepthMask(boolean flag) => glDepthMask({0})
void glDepthRange(double n, double f) => glDepthRange({0}, {1})
void glDisable(int cap) => glDisable({0})
void glDisableClientState(int cap) => glDisableClientState({0})
void glDrawArrays(int mode, int first, int count) => glDrawArrays({0}, {1}, {2})
void glDrawElements(int mode, IntBuffer i) => glDrawElements({0}, {r1}, GL_UNSIGNED_INT, {p1})
void glDrawElements(int mode, ShortBuffer i) => glDrawElements({0}, {r1}, GL_UNSIGNED_SHORT, {p1})
void glDrawElements(int mode, ByteBuffer i) => glDrawElements({0}, {r1}, GL_UNSIGNED_BYTE, {p1})
void glDrawElements(int mode, int count, int type, long offset) => glDrawElements({0}, {1}, {2}, (const void *)(uintptr_t){3})
void glEnable(int cap) => glEnable({0})
void glEnableClientState(int cap) => glEnableClientState({0})
void glEnd() => glEnd()
void glEndList() => glEndList()
void glFinish() => glFinish()
void glFlush() => glFlush()
void glFog(int name, FloatBuffer v) => glFogfv({0}, (const GLfloat *){p1})
void glFog(int name, IntBuffer v) => fog_iv({0}, (const GLint *){p1})
void glFogf(int name, float v) => glFogf({0}, {1})
void glFogi(int name, int v) => glFogi({0}, {1})
void glFrontFace(int mode) => glFrontFace({0})
void glFrustum(double l, double r, double b, double t, double n, double f) => glFrustum({0}, {1}, {2}, {3}, {4}, {5})
int glGenLists(int range) => ret_i = (jint)glGenLists({0})
void glGenTextures(IntBuffer t) => glGenTextures({r0}, (GLuint *){p0})
int glGenTextures() => GLuint n = 0; glGenTextures(1, &n); ret_i = (jint)n
void glGetBoolean(int name, ByteBuffer out) => glGetBooleanv({0}, (GLboolean *){p1})
boolean glGetBoolean(int name) => GLboolean v[16] = {}; glGetBooleanv({0}, v); ret_i = v[0]
void glGetDouble(int name, DoubleBuffer out) => glGetDoublev({0}, (GLdouble *){p1})
int glGetError() => ret_i = lwjgl_get_error()
void glGetFloat(int name, FloatBuffer out) => glGetFloatv({0}, (GLfloat *){p1})
float glGetFloat(int name) => GLfloat v[16] = {}; glGetFloatv({0}, v); ret_f = v[0]
void glGetInteger(int name, IntBuffer out) => glGetIntegerv({0}, (GLint *){p1})
int glGetInteger(int name) => GLint v[16] = {}; glGetIntegerv({0}, v); ret_i = v[0]
void glGetLight(int light, int name, FloatBuffer out) => glGetLightfv({0}, {1}, (GLfloat *){p2})
String glGetString(int name) => ret_s = (const char *)glGetString({0})
void glGetTexLevelParameter(int target, int level, int name, IntBuffer out) => glGetTexLevelParameteriv({0}, {1}, {2}, (GLint *){p3})
int glGetTexLevelParameteri(int target, int level, int name) => GLint v = 0; glGetTexLevelParameteriv({0}, {1}, {2}, &v); ret_i = v
void glGetTexImage(int target, int level, int format, int type, ByteBuffer out) => get_tex_image({0}, {1}, {2}, {3}, {p4})
void glGetTexImage(int target, int level, int format, int type, IntBuffer out) => get_tex_image({0}, {1}, {2}, {3}, {p4})
void glHint(int target, int mode) => glHint({0}, {1})
boolean glIsEnabled(int cap) => ret_i = glIsEnabled({0})
boolean glIsList(int list) => ret_i = glIsList({0})
boolean glIsTexture(int t) => ret_i = glIsTexture({0})
void glInterleavedArrays(int format, int stride, ByteBuffer p) => keep(t, 4, a[2].l), glInterleavedArrays({0}, {1}, {p2})
void glInterleavedArrays(int format, int stride, FloatBuffer p) => keep(t, 4, a[2].l), glInterleavedArrays({0}, {1}, {p2})
void glLight(int light, int name, FloatBuffer v) => glLightfv({0}, {1}, (const GLfloat *){p2})
void glLight(int light, int name, IntBuffer v) => light_iv({0}, {1}, (const GLint *){p2})
void glLightf(int light, int name, float v) => glLightf({0}, {1}, {2})
void glLighti(int light, int name, int v) => glLighti({0}, {1}, {2})
void glLightModel(int name, FloatBuffer v) => glLightModelfv({0}, (const GLfloat *){p1})
void glLightModelf(int name, float v) => glLightModelf({0}, {1})
void glLightModeli(int name, int v) => glLightModeli({0}, {1})
void glLineStipple(int factor, short pattern) => glLineStipple({0}, (GLushort){1})
void glLineWidth(float w) => glLineWidth({0})
void glListBase(int base) => glListBase({0})
void glLoadIdentity() => glLoadIdentity()
void glLoadMatrix(FloatBuffer m) => glLoadMatrixf((const GLfloat *){p0})
void glLoadMatrix(DoubleBuffer m) => glLoadMatrixd((const GLdouble *){p0})
void glMaterial(int face, int name, FloatBuffer v) => glMaterialfv({0}, {1}, (const GLfloat *){p2})
void glMaterialf(int face, int name, float v) => glMaterialf({0}, {1}, {2})
void glMateriali(int face, int name, int v) => glMateriali({0}, {1}, {2})
void glMatrixMode(int mode) => glMatrixMode({0})
void glMultMatrix(FloatBuffer m) => glMultMatrixf((const GLfloat *){p0})
void glMultMatrix(DoubleBuffer m) => glMultMatrixd((const GLdouble *){p0})
void glNewList(int list, int mode) => glNewList({0}, {1})
void glNormal3b(byte x, byte y, byte z) => glNormal3f({0} / 127.0f, {1} / 127.0f, {2} / 127.0f)
void glNormal3f(float x, float y, float z) => glNormal3f({0}, {1}, {2})
void glNormal3d(double x, double y, double z) => glNormal3d({0}, {1}, {2})
void glNormal3i(int x, int y, int z) => glNormal3f((float){0}, (float){1}, (float){2})
void glNormalPointer(int stride, ByteBuffer p) => keep(t, 1, a[1].l), glNormalPointer(GL_BYTE, {0}, {p1})
void glNormalPointer(int stride, FloatBuffer p) => keep(t, 1, a[1].l), glNormalPointer(GL_FLOAT, {0}, {p1})
void glNormalPointer(int type, int stride, ByteBuffer p) => keep(t, 1, a[2].l), glNormalPointer({0}, {1}, {p2})
void glNormalPointer(int type, int stride, long offset) => glNormalPointer({0}, {1}, (const void *)(uintptr_t){2})
void glOrtho(double l, double r, double b, double t, double n, double f) => glOrtho({0}, {1}, {2}, {3}, {4}, {5})
void glPixelStorei(int name, int v) => glPixelStorei({0}, {1})
void glPixelStoref(int name, float v) => glPixelStorei({0}, (GLint){1})
void glPointSize(float s) => glPointSize({0})
void glPolygonMode(int face, int mode) => glPolygonMode({0}, {1})
void glPolygonOffset(float factor, float units) => glPolygonOffset({0}, {1})
void glPopAttrib() => glPopAttrib()
void glPopClientAttrib() => (void)0
void glPopMatrix() => glPopMatrix()
void glPushAttrib(int mask) => glPushAttrib({0})
void glPushClientAttrib(int mask) => (void)0
void glPushMatrix() => glPushMatrix()
void glReadBuffer(int mode) => (void)0
void glDrawBuffer(int mode) => (void)0
void glReadPixels(int x, int y, int w, int h, int format, int type, ByteBuffer out) => glReadPixels({0}, {1}, {2}, {3}, {4}, {5}, {p6})
void glReadPixels(int x, int y, int w, int h, int format, int type, IntBuffer out) => glReadPixels({0}, {1}, {2}, {3}, {4}, {5}, {p6})
void glReadPixels(int x, int y, int w, int h, int format, int type, FloatBuffer out) => glReadPixels({0}, {1}, {2}, {3}, {4}, {5}, {p6})
void glRectf(float a, float b, float c, float d) => glRectf({0}, {1}, {2}, {3})
void glRecti(int a, int b, int c, int d) => glRecti({0}, {1}, {2}, {3})
void glRotatef(float angle, float x, float y, float z) => glRotatef({0}, {1}, {2}, {3})
void glRotated(double angle, double x, double y, double z) => glRotated({0}, {1}, {2}, {3})
void glScalef(float x, float y, float z) => glScalef({0}, {1}, {2})
void glScaled(double x, double y, double z) => glScaled({0}, {1}, {2})
void glScissor(int x, int y, int w, int h) => glScissor({0}, {1}, {2}, {3})
void glShadeModel(int mode) => glShadeModel({0})
void glStencilFunc(int f, int ref, int mask) => (void)0
void glStencilMask(int mask) => (void)0
void glStencilOp(int a, int b, int c) => (void)0
void glTexCoord1f(float s) => glTexCoord1f({0})
void glTexCoord2f(float s, float t) => glTexCoord2f({0}, {1})
void glTexCoord2d(double s, double t) => glTexCoord2d({0}, {1})
void glTexCoord2i(int s, int t) => glTexCoord2i({0}, {1})
void glTexCoord3f(float s, float t, float r) => glTexCoord3f({0}, {1}, {2})
void glTexCoord4f(float s, float t, float r, float q) => glTexCoord4f({0}, {1}, {2}, {3})
void glTexCoordPointer(int size, int stride, FloatBuffer p) => keep(t, 2, a[2].l), glTexCoordPointer({0}, GL_FLOAT, {1}, {p2})
void glTexCoordPointer(int size, int type, int stride, ByteBuffer p) => keep(t, 2, a[3].l), glTexCoordPointer({0}, {1}, {2}, {p3})
void glTexCoordPointer(int size, int type, int stride, long offset) => glTexCoordPointer({0}, {1}, {2}, (const void *)(uintptr_t){3})
void glTexEnvf(int target, int name, float v) => glTexEnvf({0}, {1}, {2})
void glTexEnvi(int target, int name, int v) => glTexEnvi({0}, {1}, {2})
void glTexEnv(int target, int name, FloatBuffer v) => glTexEnvfv({0}, {1}, (const GLfloat *){p2})
void glTexGeni(int coord, int name, int v) => (void)0
void glTexGenf(int coord, int name, float v) => (void)0
void glTexGen(int coord, int name, FloatBuffer v) => (void)0
void glTexImage2D(int target, int level, int internal, int w, int h, int border, int format, int type, ByteBuffer p) => glTexImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, a[8].l ? {p8} : 0)
void glTexImage2D(int target, int level, int internal, int w, int h, int border, int format, int type, IntBuffer p) => glTexImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, a[8].l ? {p8} : 0)
void glTexImage2D(int target, int level, int internal, int w, int h, int border, int format, int type, FloatBuffer p) => glTexImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, a[8].l ? {p8} : 0)
void glTexImage2D(int target, int level, int internal, int w, int h, int border, int format, int type, long offset) => glTexImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, (const void *)(uintptr_t){8})
void glTexParameterf(int target, int name, float v) => glTexParameterf({0}, {1}, {2})
void glTexParameteri(int target, int name, int v) => glTexParameteri({0}, {1}, {2})
void glTexParameter(int target, int name, FloatBuffer v) => glTexParameterf({0}, {1}, *(const GLfloat *){p2})
void glTexParameter(int target, int name, IntBuffer v) => glTexParameteri({0}, {1}, *(const GLint *){p2})
void glTexSubImage2D(int target, int level, int x, int y, int w, int h, int format, int type, ByteBuffer p) => glTexSubImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {p8})
void glTexSubImage2D(int target, int level, int x, int y, int w, int h, int format, int type, IntBuffer p) => glTexSubImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {p8})
void glTexSubImage2D(int target, int level, int x, int y, int w, int h, int format, int type, long offset) => glTexSubImage2D({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, (const void *)(uintptr_t){8})
void glTranslatef(float x, float y, float z) => glTranslatef({0}, {1}, {2})
void glTranslated(double x, double y, double z) => glTranslated({0}, {1}, {2})
void glVertex2f(float x, float y) => glVertex2f({0}, {1})
void glVertex2i(int x, int y) => glVertex2i({0}, {1})
void glVertex2d(double x, double y) => glVertex2d({0}, {1})
void glVertex3f(float x, float y, float z) => glVertex3f({0}, {1}, {2})
void glVertex3i(int x, int y, int z) => glVertex3i({0}, {1}, {2})
void glVertex3d(double x, double y, double z) => glVertex3d({0}, {1}, {2})
void glVertex4f(float x, float y, float z, float w) => glVertex4f({0}, {1}, {2}, {3})
void glVertexPointer(int size, int stride, FloatBuffer p) => keep(t, 3, a[2].l), glVertexPointer({0}, GL_FLOAT, {1}, {p2})
void glVertexPointer(int size, int stride, IntBuffer p) => keep(t, 3, a[2].l), glVertexPointer({0}, GL_INT, {1}, {p2})
void glVertexPointer(int size, int stride, ShortBuffer p) => keep(t, 3, a[2].l), glVertexPointer({0}, GL_SHORT, {1}, {p2})
void glVertexPointer(int size, int type, int stride, ByteBuffer p) => keep(t, 3, a[3].l), glVertexPointer({0}, {1}, {2}, {p3})
void glVertexPointer(int size, int type, int stride, long offset) => glVertexPointer({0}, {1}, {2}, (const void *)(uintptr_t){3})
void glViewport(int x, int y, int w, int h) => glViewport({0}, {1}, {2}, {3})
"""

def parse(spec):
    out = []
    for line in spec.strip().splitlines():
        if not line.strip(): continue
        sig, body = line.split(" => ")
        m = re.match(r"(\w+) (\w+)\((.*)\)", sig)
        ret, name, params = m.groups()
        ps = [p.strip().split() for p in params.split(",") if p.strip()]
        out.append((ret, name, ps, body))
    return out

JDESC = {"int": "I", "float": "F", "double": "D", "boolean": "Z", "long": "J", "byte": "B", "short": "S", "void": "V",
         "String": "Ljava/lang/String;"}
def jdesc(t):
    return JDESC.get(t) or "Ljava/nio/%s;" % t

def c_binding(cls, ret, name, ps, body, index):
    k = 0
    vals = {}
    pre = []
    for i, (t, n) in enumerate(ps):
        if t in ("double",):
            vals[str(i)] = "get_double(a + %d)" % k; k += 2
        elif t == "long":
            vals[str(i)] = "get_long(a + %d)" % k; k += 2
        elif t == "float":
            vals[str(i)] = "a[%d].f" % k; k += 1
        elif t in ("int", "boolean", "byte", "short"):
            vals[str(i)] = "a[%d].i" % k; k += 1
        else:
            vals[str(i)] = "a[%d].l" % k
            vals["p%d" % i] = "bufptr(t, a[%d].l)" % k
            vals["r%d" % i] = "bufrem(a[%d].l)" % k
            k += 1
    def sub(mo):
        return vals[mo.group(1)]
    c = re.sub(r"\{(\w+)\}", sub, body)
    fn = "gl_%s_%d" % (name, index)
    lines = ["static void %s(Thread *t, Slot *a)" % fn, "{", "    (void)t;"]
    if ret == "int" or ret == "boolean": lines.append("    jint ret_i = 0;")
    if ret == "float": lines.append("    jfloat ret_f = 0;")
    if ret == "String": lines.append("    const char *ret_s = 0;")
    lines.append("    %s;" % c)
    if ret in ("int", "boolean"): lines.append("    a[0].i = ret_i;")
    if ret == "float": lines.append("    a[0].f = ret_f;")
    if ret == "String": lines.append("    a[0].l = ret_s ? new_string(t, ret_s) : 0;")
    lines.append("}")
    desc = "(" + "".join(jdesc(t) for t, _ in ps) + ")" + jdesc(ret)
    return "\n".join(lines), '    {"org/lwjgl/opengl/%s", "%s", "%s", %s},' % (cls, name, desc, fn)

def java_class(cls, entries, constants, extra=""):
    lines = ["package org.lwjgl.opengl;", "", "import java.nio.*;", "", "public final class %s {" % cls, "    private %s() {}" % cls]
    for n, v in constants:
        lines.append("    public static final int %s = %s;" % (n, v))
    for ret, name, ps, body in entries:
        lines.append("    public static native %s %s(%s);" % (ret, name, ", ".join("%s %s" % (t, n) for t, n in ps)))
    lines.append(extra)
    lines.append("}")
    return "\n".join(lines) + "\n"

# GL11 constants from the Barnix GL header plus LWJGL extras.
def header_constants():
    consts = []
    seen = set()
    for line in open(os.path.join(root, "../../BDK/include/GL/gl.h")):
        m = re.match(r"#define (GL_\w+) (0x[0-9a-fA-F]+|\d+)\s*$", line)
        if m and m.group(1) not in seen and not m.group(1).startswith("GL_VERSION_"):
            seen.add(m.group(1)); consts.append((m.group(1), m.group(2)))
    return consts, seen

def main():
    consts, seen = header_constants()
    extra11 = [("GL_BYTE", "0x1400"), ("GL_UNSIGNED_BYTE", "0x1401"), ("GL_SHORT", "0x1402"), ("GL_UNSIGNED_SHORT", "0x1403"),
               ("GL_INT", "0x1404"), ("GL_UNSIGNED_INT", "0x1405"), ("GL_FLOAT", "0x1406"), ("GL_DOUBLE", "0x140A"),
               ("GL_2_BYTES", "0x1407"), ("GL_3_BYTES", "0x1408"), ("GL_4_BYTES", "0x1409"),
               ("GL_STENCIL_TEST", "0x0B90"), ("GL_ALPHA_TEST_FUNC", "0x0BC1"), ("GL_ALPHA_TEST_REF", "0x0BC2"),
               ("GL_CLAMP", "0x2900"), ("GL_REPEAT", "0x2901"), ("GL_LINE_SMOOTH", "0x0B20"), ("GL_POLYGON_SMOOTH", "0x0B41"),
               ("GL_POINT_SMOOTH", "0x0B10"), ("GL_DITHER", "0x0BD0"), ("GL_NORMALIZE", "0x0BA1"), ("GL_AUTO_NORMAL", "0x0D80"),
               ("GL_COLOR_LOGIC_OP", "0x0BF2"), ("GL_KEEP", "0x1E00"), ("GL_REPLACE", "0x1E01"), ("GL_INCR", "0x1E02"), ("GL_DECR", "0x1E03"),
               ("GL_FRONT_LEFT", "0x0400"), ("GL_BACK_LEFT", "0x0402"), ("GL_ALL_CLIENT_ATTRIB_BITS", "0xffffffff"),
               ("GL_CLIENT_VERTEX_ARRAY_BIT", "0x00000002"), ("GL_CLIENT_PIXEL_STORE_BIT", "0x00000001"),
               ("GL_TEXTURE_BIT", "0x00040000"), ("GL_COLOR_BUFFER_BIT", "0x00004000"), ("GL_DEPTH_BUFFER_BIT", "0x00000100"),
               ("GL_POLYGON_BIT", "0x00000008"), ("GL_FOG_BIT", "0x00000080"), ("GL_VIEWPORT_BIT", "0x00000800"),
               ("GL_SCISSOR_BIT", "0x00080000"), ("GL_HINT_BIT", "0x00008000"), ("GL_LINE_BIT", "0x00000004"),
               ("GL_POINT_BIT", "0x00000002"), ("GL_PIXEL_MODE_BIT", "0x00000020"), ("GL_EVAL_BIT", "0x00010000"),
               ("GL_LIST_BIT", "0x00020000"), ("GL_STENCIL_BUFFER_BIT", "0x00000400"), ("GL_ACCUM_BUFFER_BIT", "0x00000200"),
               ("GL_TEXTURE_GEN_S", "0x0C60"), ("GL_TEXTURE_GEN_T", "0x0C61"), ("GL_TEXTURE_GEN_R", "0x0C62"), ("GL_TEXTURE_GEN_Q", "0x0C63"),
               ("GL_S", "0x2000"), ("GL_T", "0x2001"), ("GL_R", "0x2002"), ("GL_Q", "0x2003"),
               ("GL_TEXTURE_GEN_MODE", "0x2500"), ("GL_OBJECT_LINEAR", "0x2401"), ("GL_EYE_LINEAR", "0x2400"), ("GL_SPHERE_MAP", "0x2402"),
               ("GL_OBJECT_PLANE", "0x2501"), ("GL_EYE_PLANE", "0x2502"), ("GL_TEXTURE_1D", "0x0DE0"), ("GL_RGBA8", "0x8058"), ("GL_RGB8", "0x8051"),
               ("GL_ALPHA8", "0x803C"), ("GL_LUMINANCE8", "0x8040"), ("GL_INTENSITY", "0x8049"), ("GL_RGB5_A1", "0x8057"),
               ("GL_CLIP_PLANE0", "0x3000"), ("GL_CLIP_PLANE1", "0x3001"), ("GL_ACCUM", "0x0100"), ("GL_LOAD", "0x0101"), ("GL_RETURN", "0x0102"),
               ("GL_MULT", "0x0103"), ("GL_ADD", "0x0104"), ("GL_EQUIV", "0x1509"), ("GL_COPY", "0x1503"), ("GL_XOR", "0x1506"),
               ("GL_INVERT", "0x150A"), ("GL_STENCIL_INDEX", "0x1901"), ("GL_DEPTH_COMPONENT", "0x1902"), ("GL_COLOR_INDEX", "0x1900"),
               ("GL_VERTEX_ARRAY", "0x8074"), ("GL_NORMAL_ARRAY", "0x8075"), ("GL_COLOR_ARRAY", "0x8076"), ("GL_TEXTURE_COORD_ARRAY", "0x8078")]
    for n, v in extra11:
        if n not in seen: seen.add(n); consts.append((n, v))
    entries = parse(GL11)
    gl11_extra = ""
    open(os.path.join(root, "lib/org/lwjgl/opengl/GL11.java"), "w").write(java_class("GL11", entries, consts, gl11_extra))
    c_funcs, c_table = [], []
    for i, (ret, name, ps, body) in enumerate(entries):
        f, row = c_binding("GL11", ret, name, ps, body, i)
        c_funcs.append(f); c_table.append(row)
    # Newer versions and extensions: see gen_gl_ext in this file's caller.
    import gl_ext
    for cls, spec, consts_ext in gl_ext.CLASSES:
        ents = parse(spec) if spec.strip() else []
        open(os.path.join(root, "lib/org/lwjgl/opengl/%s.java" % cls), "w").write(java_class(cls, ents, consts_ext))
        for i, (ret, name, ps, body) in enumerate(ents):
            f, row = c_binding(cls, ret, name, ps, body, 1000 + len(c_funcs))
            c_funcs.append(f); c_table.append(row)
    src = ["/* Generated by tools/gen_gl.py: LWJGL GL classes bound to Barnix OpenGL. */",
           '#include "jvm.h"', '#include "lwjgl_gl.h"', ""]
    src += c_funcs
    src += ["", "const NativeEntry natives_gl[] = {"] + c_table + ["    {0, 0, 0, 0}", "};", ""]
    open(os.path.join(root, "natives_gl.c"), "w").write("\n".join(src))
    print("GL bindings:", len(c_table))

sys.path.insert(0, here)
main()
