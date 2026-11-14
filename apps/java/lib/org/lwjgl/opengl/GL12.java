package org.lwjgl.opengl;

import java.nio.*;

public final class GL12 {
    private GL12() {}
    public static final int GL_CLAMP_TO_EDGE = 0x812F;
    public static final int GL_BGR = 0x80E0;
    public static final int GL_BGRA = 0x80E1;
    public static final int GL_UNSIGNED_INT_8_8_8_8 = 0x8035;
    public static final int GL_UNSIGNED_INT_8_8_8_8_REV = 0x8367;
    public static final int GL_UNSIGNED_SHORT_5_6_5 = 0x8363;
    public static final int GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033;
    public static final int GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034;
    public static final int GL_TEXTURE_MIN_LOD = 0x813A;
    public static final int GL_TEXTURE_MAX_LOD = 0x813B;
    public static final int GL_TEXTURE_BASE_LEVEL = 0x813C;
    public static final int GL_TEXTURE_MAX_LEVEL = 0x813D;
    public static final int GL_RESCALE_NORMAL = 0x803A;
    public static final int GL_LIGHT_MODEL_COLOR_CONTROL = 0x81F8;
    public static final int GL_SINGLE_COLOR = 0x81F9;
    public static final int GL_SEPARATE_SPECULAR_COLOR = 0x81FA;
    public static final int GL_MAX_ELEMENTS_VERTICES = 0x80E8;
    public static final int GL_MAX_ELEMENTS_INDICES = 0x80E9;
    public static final int GL_TEXTURE_3D = 0x806F;
    public static final int GL_TEXTURE_WRAP_R = 0x8072;
    public static final int GL_MAX_3D_TEXTURE_SIZE = 0x8073;
    public static final int GL_TEXTURE_BINDING_3D = 0x806A;
    public static native void glDrawRangeElements(int mode, int start, int end, IntBuffer i);
    public static native void glDrawRangeElements(int mode, int start, int end, ShortBuffer i);
    public static native void glDrawRangeElements(int mode, int start, int end, ByteBuffer i);
    public static native void glDrawRangeElements(int mode, int start, int end, int count, int type, long offset);

}
