package org.lwjgl.opengl;

import java.nio.*;

public final class ARBVertexBufferObject {
    private ARBVertexBufferObject() {}
    public static final int GL_ARRAY_BUFFER_ARB = 0x8892;
    public static final int GL_ELEMENT_ARRAY_BUFFER_ARB = 0x8893;
    public static final int GL_ARRAY_BUFFER_BINDING_ARB = 0x8894;
    public static final int GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB = 0x8895;
    public static final int GL_VERTEX_ARRAY_BUFFER_BINDING_ARB = 0x8896;
    public static final int GL_NORMAL_ARRAY_BUFFER_BINDING_ARB = 0x8897;
    public static final int GL_COLOR_ARRAY_BUFFER_BINDING_ARB = 0x8898;
    public static final int GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB = 0x889A;
    public static final int GL_STREAM_DRAW_ARB = 0x88E0;
    public static final int GL_STREAM_READ_ARB = 0x88E1;
    public static final int GL_STREAM_COPY_ARB = 0x88E2;
    public static final int GL_STATIC_DRAW_ARB = 0x88E4;
    public static final int GL_STATIC_READ_ARB = 0x88E5;
    public static final int GL_STATIC_COPY_ARB = 0x88E6;
    public static final int GL_DYNAMIC_DRAW_ARB = 0x88E8;
    public static final int GL_DYNAMIC_READ_ARB = 0x88E9;
    public static final int GL_DYNAMIC_COPY_ARB = 0x88EA;
    public static final int GL_READ_ONLY_ARB = 0x88B8;
    public static final int GL_WRITE_ONLY_ARB = 0x88B9;
    public static final int GL_READ_WRITE_ARB = 0x88BA;
    public static final int GL_BUFFER_SIZE_ARB = 0x8764;
    public static final int GL_BUFFER_USAGE_ARB = 0x8765;
    public static final int GL_BUFFER_ACCESS_ARB = 0x88BB;
    public static final int GL_BUFFER_MAPPED_ARB = 0x88BC;
    public static native void glGenBuffersARB(IntBuffer b);
    public static native int glGenBuffersARB();
    public static native void glDeleteBuffersARB(IntBuffer b);
    public static native void glDeleteBuffersARB(int b);
    public static native void glBindBufferARB(int target, int b);
    public static native boolean glIsBufferARB(int b);
    public static native void glBufferDataARB(int target, long size, int usage);
    public static native void glBufferDataARB(int target, ByteBuffer data, int usage);
    public static native void glBufferDataARB(int target, FloatBuffer data, int usage);
    public static native void glBufferDataARB(int target, IntBuffer data, int usage);
    public static native void glBufferDataARB(int target, ShortBuffer data, int usage);
    public static native void glBufferSubDataARB(int target, long offset, ByteBuffer data);
    public static native void glBufferSubDataARB(int target, long offset, FloatBuffer data);
    public static native void glBufferSubDataARB(int target, long offset, IntBuffer data);
    public static native void glGetBufferSubDataARB(int target, long offset, ByteBuffer data);
    public static native int glGetBufferParameteriARB(int target, int name);
    public static native void glGetBufferParameterARB(int target, int name, IntBuffer out);

}
