package org.lwjgl.opengl;

import java.nio.*;

public final class GL14 {
    private GL14() {}
    public static final int GL_GENERATE_MIPMAP = 0x8191;
    public static final int GL_GENERATE_MIPMAP_HINT = 0x8192;
    public static final int GL_BLEND_DST_RGB = 0x80C8;
    public static final int GL_BLEND_SRC_RGB = 0x80C9;
    public static final int GL_BLEND_DST_ALPHA = 0x80CA;
    public static final int GL_BLEND_SRC_ALPHA = 0x80CB;
    public static final int GL_CONSTANT_COLOR = 0x8001;
    public static final int GL_ONE_MINUS_CONSTANT_COLOR = 0x8002;
    public static final int GL_CONSTANT_ALPHA = 0x8003;
    public static final int GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004;
    public static final int GL_BLEND_COLOR = 0x8005;
    public static final int GL_FUNC_ADD = 0x8006;
    public static final int GL_MIN = 0x8007;
    public static final int GL_MAX = 0x8008;
    public static final int GL_BLEND_EQUATION = 0x8009;
    public static final int GL_FUNC_SUBTRACT = 0x800A;
    public static final int GL_FUNC_REVERSE_SUBTRACT = 0x800B;
    public static final int GL_DEPTH_COMPONENT16 = 0x81A5;
    public static final int GL_DEPTH_COMPONENT24 = 0x81A6;
    public static final int GL_DEPTH_COMPONENT32 = 0x81A7;
    public static final int GL_MIRRORED_REPEAT = 0x8370;
    public static final int GL_TEXTURE_LOD_BIAS = 0x8501;
    public static final int GL_MAX_TEXTURE_LOD_BIAS = 0x84FD;
    public static final int GL_COLOR_SUM = 0x8458;
    public static final int GL_SECONDARY_COLOR_ARRAY = 0x845E;
    public static final int GL_FOG_COORDINATE_SOURCE = 0x8450;
    public static final int GL_FOG_COORDINATE = 0x8451;
    public static final int GL_FRAGMENT_DEPTH = 0x8452;
    public static final int GL_INCR_WRAP = 0x8507;
    public static final int GL_DECR_WRAP = 0x8508;
    public static final int GL_POINT_SIZE_MIN = 0x8126;
    public static final int GL_POINT_SIZE_MAX = 0x8127;
    public static final int GL_POINT_FADE_THRESHOLD_SIZE = 0x8128;
    public static final int GL_TEXTURE_DEPTH_SIZE = 0x884A;
    public static final int GL_DEPTH_TEXTURE_MODE = 0x884B;
    public static final int GL_TEXTURE_COMPARE_MODE = 0x884C;
    public static final int GL_TEXTURE_COMPARE_FUNC = 0x884D;
    public static final int GL_COMPARE_R_TO_TEXTURE = 0x884E;
    public static native void glBlendEquation(int mode);
    public static native void glBlendColor(float r, float g, float b, float a);
    public static native void glBlendFuncSeparate(int sRGB, int dRGB, int sA, int dA);
    public static native void glMultiDrawArrays(int mode, IntBuffer first, IntBuffer count);
    public static native void glWindowPos2f(float x, float y);
    public static native void glWindowPos2i(int x, int y);
    public static native void glWindowPos3f(float x, float y, float z);
    public static native void glPointParameterf(int name, float v);
    public static native void glSecondaryColor3f(float r, float g, float b);
    public static native void glFogCoordf(float c);

}
