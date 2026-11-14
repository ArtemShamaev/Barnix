"""LWJGL classes for OpenGL 1.2-1.5 and the ARB extensions Barnix OpenGL implements."""

def c(*pairs):
    return [(pairs[i], pairs[i + 1]) for i in range(0, len(pairs), 2)]

GL12 = r"""
void glDrawRangeElements(int mode, int start, int end, IntBuffer i) => glDrawRangeElements({0}, {1}, {2}, {r3}, GL_UNSIGNED_INT, {p3})
void glDrawRangeElements(int mode, int start, int end, ShortBuffer i) => glDrawRangeElements({0}, {1}, {2}, {r3}, GL_UNSIGNED_SHORT, {p3})
void glDrawRangeElements(int mode, int start, int end, ByteBuffer i) => glDrawRangeElements({0}, {1}, {2}, {r3}, GL_UNSIGNED_BYTE, {p3})
void glDrawRangeElements(int mode, int start, int end, int count, int type, long offset) => glDrawRangeElements({0}, {1}, {2}, {3}, {4}, (const void *)(uintptr_t){5})
"""
GL12_C = c("GL_CLAMP_TO_EDGE", "0x812F", "GL_BGR", "0x80E0", "GL_BGRA", "0x80E1", "GL_UNSIGNED_INT_8_8_8_8", "0x8035",
           "GL_UNSIGNED_INT_8_8_8_8_REV", "0x8367", "GL_UNSIGNED_SHORT_5_6_5", "0x8363", "GL_UNSIGNED_SHORT_4_4_4_4", "0x8033",
           "GL_UNSIGNED_SHORT_5_5_5_1", "0x8034", "GL_TEXTURE_MIN_LOD", "0x813A", "GL_TEXTURE_MAX_LOD", "0x813B",
           "GL_TEXTURE_BASE_LEVEL", "0x813C", "GL_TEXTURE_MAX_LEVEL", "0x813D", "GL_RESCALE_NORMAL", "0x803A",
           "GL_LIGHT_MODEL_COLOR_CONTROL", "0x81F8", "GL_SINGLE_COLOR", "0x81F9", "GL_SEPARATE_SPECULAR_COLOR", "0x81FA",
           "GL_MAX_ELEMENTS_VERTICES", "0x80E8", "GL_MAX_ELEMENTS_INDICES", "0x80E9", "GL_TEXTURE_3D", "0x806F",
           "GL_TEXTURE_WRAP_R", "0x8072", "GL_MAX_3D_TEXTURE_SIZE", "0x8073", "GL_TEXTURE_BINDING_3D", "0x806A")

GL13 = r"""
void glActiveTexture(int unit) => glActiveTexture({0})
void glClientActiveTexture(int unit) => glClientActiveTexture({0})
void glMultiTexCoord1f(int unit, float s) => glMultiTexCoord4f({0}, {1}, 0, 0, 1)
void glMultiTexCoord2f(int unit, float s, float t) => glMultiTexCoord4f({0}, {1}, {2}, 0, 1)
void glMultiTexCoord2d(int unit, double s, double t) => glMultiTexCoord4f({0}, (float){1}, (float){2}, 0, 1)
void glMultiTexCoord3f(int unit, float s, float t, float r) => glMultiTexCoord4f({0}, {1}, {2}, {3}, 1)
void glMultiTexCoord4f(int unit, float s, float t, float r, float q) => glMultiTexCoord4f({0}, {1}, {2}, {3}, {4})
void glLoadTransposeMatrix(FloatBuffer m) => glLoadTransposeMatrixf((const GLfloat *){p0})
void glMultTransposeMatrix(FloatBuffer m) => glMultTransposeMatrixf((const GLfloat *){p0})
void glSampleCoverage(float value, boolean invert) => (void)0
"""
GL13_C = [("GL_TEXTURE%d" % i, "0x%X" % (0x84C0 + i)) for i in range(32)] + c(
    "GL_ACTIVE_TEXTURE", "0x84E0", "GL_CLIENT_ACTIVE_TEXTURE", "0x84E1", "GL_MAX_TEXTURE_UNITS", "0x84E2",
    "GL_TRANSPOSE_MODELVIEW_MATRIX", "0x84E3", "GL_TRANSPOSE_PROJECTION_MATRIX", "0x84E4", "GL_TRANSPOSE_TEXTURE_MATRIX", "0x84E5",
    "GL_MULTISAMPLE", "0x809D", "GL_SAMPLE_ALPHA_TO_COVERAGE", "0x809E", "GL_CLAMP_TO_BORDER", "0x812D",
    "GL_COMBINE", "0x8570", "GL_COMBINE_RGB", "0x8571", "GL_COMBINE_ALPHA", "0x8572", "GL_SOURCE0_RGB", "0x8580",
    "GL_SOURCE1_RGB", "0x8581", "GL_SOURCE2_RGB", "0x8582", "GL_SOURCE0_ALPHA", "0x8588", "GL_SOURCE1_ALPHA", "0x8589",
    "GL_SOURCE2_ALPHA", "0x858A", "GL_OPERAND0_RGB", "0x8590", "GL_OPERAND1_RGB", "0x8591", "GL_OPERAND2_RGB", "0x8592",
    "GL_OPERAND0_ALPHA", "0x8598", "GL_OPERAND1_ALPHA", "0x8599", "GL_OPERAND2_ALPHA", "0x859A", "GL_RGB_SCALE", "0x8573",
    "GL_ADD_SIGNED", "0x8574", "GL_INTERPOLATE", "0x8575", "GL_SUBTRACT", "0x84E7", "GL_CONSTANT", "0x8576",
    "GL_PRIMARY_COLOR", "0x8577", "GL_PREVIOUS", "0x8578", "GL_DOT3_RGB", "0x86AE", "GL_DOT3_RGBA", "0x86AF",
    "GL_TEXTURE_CUBE_MAP", "0x8513", "GL_COMPRESSED_RGB", "0x84ED", "GL_COMPRESSED_RGBA", "0x84EE")

GL14 = r"""
void glBlendEquation(int mode) => glBlendEquation({0})
void glBlendColor(float r, float g, float b, float a) => glBlendColor({0}, {1}, {2}, {3})
void glBlendFuncSeparate(int sRGB, int dRGB, int sA, int dA) => glBlendFuncSeparate({0}, {1}, {2}, {3})
void glMultiDrawArrays(int mode, IntBuffer first, IntBuffer count) => glMultiDrawArrays({0}, (const GLint *){p1}, (const GLsizei *){p2}, {r2})
void glWindowPos2f(float x, float y) => glWindowPos3f({0}, {1}, 0)
void glWindowPos2i(int x, int y) => glWindowPos3f((float){0}, (float){1}, 0)
void glWindowPos3f(float x, float y, float z) => glWindowPos3f({0}, {1}, {2})
void glPointParameterf(int name, float v) => (void)0
void glSecondaryColor3f(float r, float g, float b) => glSecondaryColor3f({0}, {1}, {2})
void glFogCoordf(float c) => glFogCoordf({0})
"""
GL14_C = c("GL_GENERATE_MIPMAP", "0x8191", "GL_GENERATE_MIPMAP_HINT", "0x8192", "GL_BLEND_DST_RGB", "0x80C8",
           "GL_BLEND_SRC_RGB", "0x80C9", "GL_BLEND_DST_ALPHA", "0x80CA", "GL_BLEND_SRC_ALPHA", "0x80CB",
           "GL_CONSTANT_COLOR", "0x8001", "GL_ONE_MINUS_CONSTANT_COLOR", "0x8002", "GL_CONSTANT_ALPHA", "0x8003",
           "GL_ONE_MINUS_CONSTANT_ALPHA", "0x8004", "GL_BLEND_COLOR", "0x8005", "GL_FUNC_ADD", "0x8006", "GL_MIN", "0x8007",
           "GL_MAX", "0x8008", "GL_BLEND_EQUATION", "0x8009", "GL_FUNC_SUBTRACT", "0x800A", "GL_FUNC_REVERSE_SUBTRACT", "0x800B",
           "GL_DEPTH_COMPONENT16", "0x81A5", "GL_DEPTH_COMPONENT24", "0x81A6", "GL_DEPTH_COMPONENT32", "0x81A7",
           "GL_MIRRORED_REPEAT", "0x8370", "GL_TEXTURE_LOD_BIAS", "0x8501", "GL_MAX_TEXTURE_LOD_BIAS", "0x84FD",
           "GL_COLOR_SUM", "0x8458", "GL_SECONDARY_COLOR_ARRAY", "0x845E", "GL_FOG_COORDINATE_SOURCE", "0x8450",
           "GL_FOG_COORDINATE", "0x8451", "GL_FRAGMENT_DEPTH", "0x8452", "GL_INCR_WRAP", "0x8507", "GL_DECR_WRAP", "0x8508",
           "GL_POINT_SIZE_MIN", "0x8126", "GL_POINT_SIZE_MAX", "0x8127", "GL_POINT_FADE_THRESHOLD_SIZE", "0x8128",
           "GL_TEXTURE_DEPTH_SIZE", "0x884A", "GL_DEPTH_TEXTURE_MODE", "0x884B", "GL_TEXTURE_COMPARE_MODE", "0x884C",
           "GL_TEXTURE_COMPARE_FUNC", "0x884D", "GL_COMPARE_R_TO_TEXTURE", "0x884E")

def buffers(suffix):
    s = suffix
    return r"""
void glGenBuffers%(s)s(IntBuffer b) => glGenBuffers({r0}, (GLuint *){p0})
int glGenBuffers%(s)s() => GLuint n = 0; glGenBuffers(1, &n); ret_i = (jint)n
void glDeleteBuffers%(s)s(IntBuffer b) => glDeleteBuffers({r0}, (const GLuint *){p0})
void glDeleteBuffers%(s)s(int b) => glDeleteBuffers(1, (const GLuint *)&a[0].i)
void glBindBuffer%(s)s(int target, int b) => glBindBuffer({0}, {1})
boolean glIsBuffer%(s)s(int b) => ret_i = glIsBuffer({0})
void glBufferData%(s)s(int target, long size, int usage) => glBufferData({0}, (GLsizeiptr){1}, 0, {2})
void glBufferData%(s)s(int target, ByteBuffer data, int usage) => glBufferData({0}, bufbytes(a[1].l), {p1}, {2})
void glBufferData%(s)s(int target, FloatBuffer data, int usage) => glBufferData({0}, bufbytes(a[1].l), {p1}, {2})
void glBufferData%(s)s(int target, IntBuffer data, int usage) => glBufferData({0}, bufbytes(a[1].l), {p1}, {2})
void glBufferData%(s)s(int target, ShortBuffer data, int usage) => glBufferData({0}, bufbytes(a[1].l), {p1}, {2})
void glBufferSubData%(s)s(int target, long offset, ByteBuffer data) => glBufferSubData({0}, (GLintptr){1}, bufbytes(a[3].l), {p2})
void glBufferSubData%(s)s(int target, long offset, FloatBuffer data) => glBufferSubData({0}, (GLintptr){1}, bufbytes(a[3].l), {p2})
void glBufferSubData%(s)s(int target, long offset, IntBuffer data) => glBufferSubData({0}, (GLintptr){1}, bufbytes(a[3].l), {p2})
void glGetBufferSubData%(s)s(int target, long offset, ByteBuffer data) => glGetBufferSubData({0}, (GLintptr){1}, bufbytes(a[3].l), {p2})
int glGetBufferParameteri%(s)s(int target, int name) => GLint v = 0; glGetBufferParameteriv({0}, {1}, &v); ret_i = v
void glGetBufferParameter%(s)s(int target, int name, IntBuffer out) => glGetBufferParameteriv({0}, {1}, (GLint *){p2})
""" % {"s": s}

def queries(suffix):
    s = suffix
    return r"""
void glGenQueries%(s)s(IntBuffer q) => glGenQueries({r0}, (GLuint *){p0})
int glGenQueries%(s)s() => GLuint n = 0; glGenQueries(1, &n); ret_i = (jint)n
void glDeleteQueries%(s)s(IntBuffer q) => glDeleteQueries({r0}, (const GLuint *){p0})
void glDeleteQueries%(s)s(int q) => glDeleteQueries(1, (const GLuint *)&a[0].i)
boolean glIsQuery%(s)s(int q) => ret_i = glIsQuery({0})
void glBeginQuery%(s)s(int target, int q) => glBeginQuery({0}, {1})
void glEndQuery%(s)s(int target) => glEndQuery({0})
void glGetQuery%(s)s(int target, int name, IntBuffer out) => glGetQueryiv({0}, {1}, (GLint *){p2})
int glGetQueryi%(s)s(int target, int name) => GLint v = 0; glGetQueryiv({0}, {1}, &v); ret_i = v
void glGetQueryObject%(s)s(int q, int name, IntBuffer out) => glGetQueryObjectiv({0}, {1}, (GLint *){p2})
int glGetQueryObjecti%(s)s(int q, int name) => GLint v = 0; glGetQueryObjectiv({0}, {1}, &v); ret_i = v
void glGetQueryObjectu%(s)s(int q, int name, IntBuffer out) => glGetQueryObjectuiv({0}, {1}, (GLuint *){p2})
int glGetQueryObjectui%(s)s(int q, int name) => GLuint v = 0; glGetQueryObjectuiv({0}, {1}, &v); ret_i = (jint)v
""" % {"s": s}

GL15 = buffers("") + queries("") + r"""
void glGetQueryObject(int q, int name, IntBuffer out) => glGetQueryObjectiv({0}, {1}, (GLint *){p2})
void glGetQueryObjectu(int q, int name, IntBuffer out) => glGetQueryObjectuiv({0}, {1}, (GLuint *){p2})
"""
# the generic names above already include glGetQueryObject/glGetQueryObjectu for suffix "", drop duplicates
GL15 = "\n".join(dict.fromkeys(GL15.strip().splitlines()))

BUF_C = ["GL_ARRAY_BUFFER", "0x8892", "GL_ELEMENT_ARRAY_BUFFER", "0x8893", "GL_ARRAY_BUFFER_BINDING", "0x8894",
         "GL_ELEMENT_ARRAY_BUFFER_BINDING", "0x8895", "GL_VERTEX_ARRAY_BUFFER_BINDING", "0x8896",
         "GL_NORMAL_ARRAY_BUFFER_BINDING", "0x8897", "GL_COLOR_ARRAY_BUFFER_BINDING", "0x8898",
         "GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING", "0x889A", "GL_STREAM_DRAW", "0x88E0", "GL_STREAM_READ", "0x88E1",
         "GL_STREAM_COPY", "0x88E2", "GL_STATIC_DRAW", "0x88E4", "GL_STATIC_READ", "0x88E5", "GL_STATIC_COPY", "0x88E6",
         "GL_DYNAMIC_DRAW", "0x88E8", "GL_DYNAMIC_READ", "0x88E9", "GL_DYNAMIC_COPY", "0x88EA", "GL_READ_ONLY", "0x88B8",
         "GL_WRITE_ONLY", "0x88B9", "GL_READ_WRITE", "0x88BA", "GL_BUFFER_SIZE", "0x8764", "GL_BUFFER_USAGE", "0x8765",
         "GL_BUFFER_ACCESS", "0x88BB", "GL_BUFFER_MAPPED", "0x88BC"]
QUERY_C = ["GL_QUERY_COUNTER_BITS", "0x8864", "GL_CURRENT_QUERY", "0x8865", "GL_QUERY_RESULT", "0x8866",
           "GL_QUERY_RESULT_AVAILABLE", "0x8867", "GL_SAMPLES_PASSED", "0x8914"]
GL15_C = c(*(BUF_C + QUERY_C))

def arb(names):
    out = []
    for i in range(0, len(names), 2):
        out.append((names[i] + "_ARB", names[i + 1]))
    return out

ARB_VBO = buffers("ARB")
ARB_OQ = queries("ARB")
ARB_MT = r"""
void glActiveTextureARB(int unit) => glActiveTexture({0})
void glClientActiveTextureARB(int unit) => glClientActiveTexture({0})
void glMultiTexCoord1fARB(int unit, float s) => glMultiTexCoord4f({0}, {1}, 0, 0, 1)
void glMultiTexCoord2fARB(int unit, float s, float t) => glMultiTexCoord4f({0}, {1}, {2}, 0, 1)
void glMultiTexCoord3fARB(int unit, float s, float t, float r) => glMultiTexCoord4f({0}, {1}, {2}, {3}, 1)
void glMultiTexCoord4fARB(int unit, float s, float t, float r, float q) => glMultiTexCoord4f({0}, {1}, {2}, {3}, {4})
"""
ARB_MT_C = [("GL_TEXTURE%d_ARB" % i, "0x%X" % (0x84C0 + i)) for i in range(32)] + arb(
    ["GL_ACTIVE_TEXTURE", "0x84E0", "GL_CLIENT_ACTIVE_TEXTURE", "0x84E1", "GL_MAX_TEXTURE_UNITS", "0x84E2"])

CLASSES = [
    ("GL12", GL12, GL12_C),
    ("GL13", GL13, GL13_C),
    ("GL14", GL14, GL14_C),
    ("GL15", GL15, GL15_C),
    ("ARBVertexBufferObject", ARB_VBO, arb(BUF_C)),
    ("ARBBufferObject", "", arb(BUF_C)),
    ("ARBOcclusionQuery", ARB_OQ, arb(QUERY_C)),
    ("ARBMultitexture", ARB_MT, ARB_MT_C),
    ("EXTTextureFilterAnisotropic", "", c("GL_TEXTURE_MAX_ANISOTROPY_EXT", "0x84FE", "GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT", "0x84FF")),
    ("NVFogDistance", "", c("GL_FOG_DISTANCE_MODE_NV", "0x855A", "GL_EYE_RADIAL_NV", "0x855B", "GL_EYE_PLANE_ABSOLUTE_NV", "0x855C")),
    ("EXTBgra", "", c("GL_BGR_EXT", "0x80E0", "GL_BGRA_EXT", "0x80E1")),
    ("EXTAbgr", "", c("GL_ABGR_EXT", "0x8000")),
]
