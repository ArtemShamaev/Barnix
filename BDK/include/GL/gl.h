#ifndef __gl_h_
#define __gl_h_
#ifdef __cplusplus
extern "C" {
#endif
/* Barnix OpenGL: the OpenGL 1.5 fixed-function API (1.1 plus multitexture,
 * buffer objects, occlusion queries, blend equations, BGRA and edge clamp),
 * rendered in software on the x87 FPU and spread over every processor core. Create the context with
 * <barnix_opengl.h> (bxgl_open), then use ordinary GL calls. */
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;

#define GL_VERSION_1_1 1
#define GL_VERSION_1_2 1
#define GL_VERSION_1_3 1
#define GL_VERSION_1_4 1
#define GL_VERSION_1_5 1
typedef long GLintptr;
typedef long GLsizeiptr;
#define GL_FALSE 0
#define GL_TRUE 1

/* Primitives */
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009

/* Clear bits */
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_CURRENT_BIT 0x00000001
#define GL_LIGHTING_BIT 0x00000040
#define GL_ENABLE_BIT 0x00002000
#define GL_TRANSFORM_BIT 0x00001000
#define GL_ALL_ATTRIB_BITS 0x000fffff

/* Errors */
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW 0x0503
#define GL_STACK_UNDERFLOW 0x0504
#define GL_OUT_OF_MEMORY 0x0505

/* Comparison functions */
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

/* Blending */
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308

/* Faces and polygons */
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01

/* Capabilities (glEnable) */
#define GL_POINT_SMOOTH 0x0B10
#define GL_LINE_SMOOTH 0x0B20
#define GL_LINE_STIPPLE 0x0B24
#define GL_POLYGON_SMOOTH 0x0B41
#define GL_CULL_FACE 0x0B44
#define GL_LIGHTING 0x0B50
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_COLOR_MATERIAL 0x0B57
#define GL_FOG 0x0B60
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_NORMALIZE 0x0BA1
#define GL_ALPHA_TEST 0x0BC0
#define GL_DITHER 0x0BD0
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE_2D 0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_RESCALE_NORMAL 0x803A
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61

/* Lights and materials */
#define GL_LIGHT0 0x4000
#define GL_LIGHT1 0x4001
#define GL_LIGHT2 0x4002
#define GL_LIGHT3 0x4003
#define GL_LIGHT4 0x4004
#define GL_LIGHT5 0x4005
#define GL_LIGHT6 0x4006
#define GL_LIGHT7 0x4007
#define GL_MAX_LIGHTS 0x0D31
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_POSITION 0x1203
#define GL_SPOT_DIRECTION 0x1204
#define GL_SPOT_EXPONENT 0x1205
#define GL_SPOT_CUTOFF 0x1206
#define GL_CONSTANT_ATTENUATION 0x1207
#define GL_LINEAR_ATTENUATION 0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#define GL_EMISSION 0x1600
#define GL_SHININESS 0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602

/* Fog */
#define GL_FOG_INDEX 0x0B61
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_LINEAR 0x2601

/* Matrices */
#define GL_MATRIX_MODE 0x0BA0
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_TEXTURE_MATRIX 0x0BA8
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38

/* State queries */
#define GL_CURRENT_COLOR 0x0B00
#define GL_CURRENT_NORMAL 0x0B02
#define GL_CURRENT_TEXTURE_COORDS 0x0B03
#define GL_POINT_SIZE 0x0B11
#define GL_LINE_WIDTH 0x0B21
#define GL_POLYGON_MODE 0x0B40
#define GL_CULL_FACE_MODE 0x0B45
#define GL_FRONT_FACE 0x0B46
#define GL_SHADE_MODEL 0x0B54
#define GL_DEPTH_RANGE 0x0B70
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#define GL_DEPTH_FUNC 0x0B74
#define GL_VIEWPORT 0x0BA2
#define GL_ALPHA_TEST_FUNC 0x0BC1
#define GL_ALPHA_TEST_REF 0x0BC2
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_RED_BITS 0x0D52
#define GL_GREEN_BITS 0x0D53
#define GL_BLUE_BITS 0x0D54
#define GL_ALPHA_BITS 0x0D55
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_MAX_LIST_NESTING 0x0B31
#define GL_LIST_INDEX 0x0B33
#define GL_LIST_MODE 0x0B30

/* Hints */
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_POINT_SMOOTH_HINT 0x0C51
#define GL_LINE_SMOOTH_HINT 0x0C52
#define GL_POLYGON_SMOOTH_HINT 0x0C53
#define GL_FOG_HINT 0x0C54
#define GL_DONT_CARE 0x1100
#define GL_FASTEST 0x1101
#define GL_NICEST 0x1102

/* Strings */
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03

/* Data types */
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_DOUBLE 0x140A

/* Pixel formats */
#define GL_COLOR_INDEX 0x1900
#define GL_STENCIL_INDEX 0x1901
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED 0x1903
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ROW_LENGTH 0x0CF2

/* Textures */
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_REPLACE 0x1E01
#define GL_ADD 0x0104
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP 0x2900
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001

/* Vertex arrays */
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_V2F 0x2A20
#define GL_V3F 0x2A21
#define GL_C4UB_V3F 0x2A23
#define GL_C3F_V3F 0x2A24
#define GL_N3F_V3F 0x2A25
#define GL_T2F_V3F 0x2A27
#define GL_T2F_C3F_V3F 0x2A2A
#define GL_T2F_N3F_V3F 0x2A2B

/* Display lists */
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301

/* State */
void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
GLenum glGetError(void);
const GLubyte *glGetString(GLenum name);
void glGetBooleanv(GLenum name, GLboolean *out);
void glGetIntegerv(GLenum name, GLint *out);
void glGetFloatv(GLenum name, GLfloat *out);
void glGetDoublev(GLenum name, GLdouble *out);
void glHint(GLenum target, GLenum mode);
void glFlush(void);
void glFinish(void);

/* Framebuffer */
void glClear(GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepth(GLclampd depth);
void glClearDepthf(GLclampf depth);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glDepthMask(GLboolean flag);
void glDepthFunc(GLenum func);
void glDepthRange(GLclampd near_value, GLclampd far_value);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glAlphaFunc(GLenum func, GLclampf ref);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void glPixelStorei(GLenum name, GLint value);
void glPolygonOffset(GLfloat factor, GLfloat units);

/* Rasterization */
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glShadeModel(GLenum mode);
void glPointSize(GLfloat size);
void glLineWidth(GLfloat width);
void glLineStipple(GLint factor, GLushort pattern);

/* Matrices */
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadMatrixf(const GLfloat *m);
void glLoadMatrixd(const GLdouble *m);
void glMultMatrixf(const GLfloat *m);
void glMultMatrixd(const GLdouble *m);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glTranslated(GLdouble x, GLdouble y, GLdouble z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glScaled(GLdouble x, GLdouble y, GLdouble z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_value, GLdouble far_value);
void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_value, GLdouble far_value);

/* Immediate mode */
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2i(GLint x, GLint y);
void glVertex2d(GLdouble x, GLdouble y);
void glVertex2fv(const GLfloat *v);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3i(GLint x, GLint y, GLint z);
void glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void glVertex3fv(const GLfloat *v);
void glVertex3dv(const GLdouble *v);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glVertex4fv(const GLfloat *v);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor3d(GLdouble r, GLdouble g, GLdouble b);
void glColor3fv(const GLfloat *v);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor3ubv(const GLubyte *v);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a);
void glColor4fv(const GLfloat *v);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glColor4ubv(const GLubyte *v);
void glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void glNormal3d(GLdouble x, GLdouble y, GLdouble z);
void glNormal3fv(const GLfloat *v);
void glTexCoord1f(GLfloat s);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord2i(GLint s, GLint t);
void glTexCoord2fv(const GLfloat *v);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2);

/* Vertex arrays */
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer);
void glArrayElement(GLint i);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

/* Lighting and fog */
void glLightf(GLenum light, GLenum name, GLfloat value);
void glLighti(GLenum light, GLenum name, GLint value);
void glLightfv(GLenum light, GLenum name, const GLfloat *values);
void glGetLightfv(GLenum light, GLenum name, GLfloat *values);
void glLightModelf(GLenum name, GLfloat value);
void glLightModeli(GLenum name, GLint value);
void glLightModelfv(GLenum name, const GLfloat *values);
void glMaterialf(GLenum face, GLenum name, GLfloat value);
void glMateriali(GLenum face, GLenum name, GLint value);
void glMaterialfv(GLenum face, GLenum name, const GLfloat *values);
void glColorMaterial(GLenum face, GLenum mode);
void glFogf(GLenum name, GLfloat value);
void glFogi(GLenum name, GLint value);
void glFogfv(GLenum name, const GLfloat *values);

/* Textures */
void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
GLboolean glIsTexture(GLuint texture);
void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels);
void glCopyTexImage2D(GLenum target, GLint level, GLenum internal_format, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border);
void glTexParameteri(GLenum target, GLenum name, GLint value);
void glTexParameterf(GLenum target, GLenum name, GLfloat value);
void glTexEnvi(GLenum target, GLenum name, GLint value);
void glTexEnvf(GLenum target, GLenum name, GLfloat value);
void glTexEnvfv(GLenum target, GLenum name, const GLfloat *values);
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum name, GLint *value);

/* Display lists */
GLuint glGenLists(GLsizei range);
void glNewList(GLuint list, GLenum mode);
void glEndList(void);
void glCallList(GLuint list);
void glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
void glListBase(GLuint base);
void glDeleteLists(GLuint list, GLsizei range);
GLboolean glIsList(GLuint list);

/* Attribute stack (current color, enables, lighting, transform mode) */
void glPushAttrib(GLbitfield mask);
void glPopAttrib(void);

/* ---- OpenGL 1.2 */
#define GL_UNSIGNED_INT_8_8_8_8 0x8035
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_TEXTURE_MIN_LOD 0x813A
#define GL_TEXTURE_MAX_LOD 0x813B
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_LIGHT_MODEL_COLOR_CONTROL 0x81F8
#define GL_SINGLE_COLOR 0x81F9
#define GL_SEPARATE_SPECULAR_COLOR 0x81FA
#define GL_MAX_ELEMENTS_VERTICES 0x80E8
#define GL_MAX_ELEMENTS_INDICES 0x80E9
#define GL_TEXTURE_STACK_DEPTH 0x0BA5
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0D39
#define GL_TEXTURE_GEN_R 0x0C62
#define GL_TEXTURE_GEN_Q 0x0C63
#define GL_AUTO_NORMAL 0x0D80
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_CLIP_PLANE0 0x3000
#define GL_CLIP_PLANE1 0x3001
void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);

/* ---- OpenGL 1.3: two texture units */
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_MAX_TEXTURE_UNITS 0x84E2
#define GL_TRANSPOSE_MODELVIEW_MATRIX 0x84E3
#define GL_TRANSPOSE_PROJECTION_MATRIX 0x84E4
#define GL_TRANSPOSE_TEXTURE_MATRIX 0x84E5
#define GL_MULTISAMPLE 0x809D
#define GL_CLAMP_TO_BORDER 0x812D
void glActiveTexture(GLenum unit);
void glClientActiveTexture(GLenum unit);
void glMultiTexCoord4f(GLenum unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glMultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t);
void glLoadTransposeMatrixf(const GLfloat *m);
void glMultTransposeMatrixf(const GLfloat *m);

/* ---- OpenGL 1.4 */
#define GL_GENERATE_MIPMAP 0x8191
#define GL_GENERATE_MIPMAP_HINT 0x8192
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_BLEND_COLOR 0x8005
#define GL_FUNC_ADD 0x8006
#define GL_MIN 0x8007
#define GL_MAX 0x8008
#define GL_BLEND_EQUATION 0x8009
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_MIRRORED_REPEAT 0x8370
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_COLOR_SUM 0x8458
#define GL_FOG_COORDINATE_SOURCE 0x8450
#define GL_FOG_COORDINATE 0x8451
#define GL_FRAGMENT_DEPTH 0x8452
void glBlendEquation(GLenum mode);
void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glBlendFuncSeparate(GLenum srgb, GLenum drgb, GLenum salpha, GLenum dalpha);
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z);
void glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b);
void glFogCoordf(GLfloat coord);

/* ---- OpenGL 1.5: buffer objects and occlusion queries */
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING 0x8896
#define GL_NORMAL_ARRAY_BUFFER_BINDING 0x8897
#define GL_COLOR_ARRAY_BUFFER_BINDING 0x8898
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#define GL_STREAM_DRAW 0x88E0
#define GL_STREAM_READ 0x88E1
#define GL_STREAM_COPY 0x88E2
#define GL_STATIC_DRAW 0x88E4
#define GL_STATIC_READ 0x88E5
#define GL_STATIC_COPY 0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA
#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#define GL_BUFFER_SIZE 0x8764
#define GL_BUFFER_USAGE 0x8765
#define GL_BUFFER_ACCESS 0x88BB
#define GL_BUFFER_MAPPED 0x88BC
#define GL_QUERY_COUNTER_BITS 0x8864
#define GL_CURRENT_QUERY 0x8865
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_SAMPLES_PASSED 0x8914
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
GLboolean glIsBuffer(GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data);
void glGetBufferParameteriv(GLenum target, GLenum name, GLint *value);
GLvoid *glMapBuffer(GLenum target, GLenum access);
GLboolean glUnmapBuffer(GLenum target);
void glGenQueries(GLsizei n, GLuint *ids);
void glDeleteQueries(GLsizei n, const GLuint *ids);
GLboolean glIsQuery(GLuint id);
void glBeginQuery(GLenum target, GLuint id);
void glEndQuery(GLenum target);
void glGetQueryiv(GLenum target, GLenum name, GLint *value);
void glGetQueryObjectiv(GLuint id, GLenum name, GLint *value);
void glGetQueryObjectuiv(GLuint id, GLenum name, GLuint *value);
#ifdef __cplusplus
}
#endif
#endif
