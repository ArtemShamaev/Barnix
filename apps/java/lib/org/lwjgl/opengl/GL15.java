package org.lwjgl.opengl;

import java.nio.*;

public final class GL15 {
    private GL15() {}
    public static final int GL_ARRAY_BUFFER = 0x8892;
    public static final int GL_ELEMENT_ARRAY_BUFFER = 0x8893;
    public static final int GL_ARRAY_BUFFER_BINDING = 0x8894;
    public static final int GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
    public static final int GL_VERTEX_ARRAY_BUFFER_BINDING = 0x8896;
    public static final int GL_NORMAL_ARRAY_BUFFER_BINDING = 0x8897;
    public static final int GL_COLOR_ARRAY_BUFFER_BINDING = 0x8898;
    public static final int GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING = 0x889A;
    public static final int GL_STREAM_DRAW = 0x88E0;
    public static final int GL_STREAM_READ = 0x88E1;
    public static final int GL_STREAM_COPY = 0x88E2;
    public static final int GL_STATIC_DRAW = 0x88E4;
    public static final int GL_STATIC_READ = 0x88E5;
    public static final int GL_STATIC_COPY = 0x88E6;
    public static final int GL_DYNAMIC_DRAW = 0x88E8;
    public static final int GL_DYNAMIC_READ = 0x88E9;
    public static final int GL_DYNAMIC_COPY = 0x88EA;
    public static final int GL_READ_ONLY = 0x88B8;
    public static final int GL_WRITE_ONLY = 0x88B9;
    public static final int GL_READ_WRITE = 0x88BA;
    public static final int GL_BUFFER_SIZE = 0x8764;
    public static final int GL_BUFFER_USAGE = 0x8765;
    public static final int GL_BUFFER_ACCESS = 0x88BB;
    public static final int GL_BUFFER_MAPPED = 0x88BC;
    public static final int GL_QUERY_COUNTER_BITS = 0x8864;
    public static final int GL_CURRENT_QUERY = 0x8865;
    public static final int GL_QUERY_RESULT = 0x8866;
    public static final int GL_QUERY_RESULT_AVAILABLE = 0x8867;
    public static final int GL_SAMPLES_PASSED = 0x8914;
    public static native void glGenBuffers(IntBuffer b);
    public static native int glGenBuffers();
    public static native void glDeleteBuffers(IntBuffer b);
    public static native void glDeleteBuffers(int b);
    public static native void glBindBuffer(int target, int b);
    public static native boolean glIsBuffer(int b);
    public static native void glBufferData(int target, long size, int usage);
    public static native void glBufferData(int target, ByteBuffer data, int usage);
    public static native void glBufferData(int target, FloatBuffer data, int usage);
    public static native void glBufferData(int target, IntBuffer data, int usage);
    public static native void glBufferData(int target, ShortBuffer data, int usage);
    public static native void glBufferSubData(int target, long offset, ByteBuffer data);
    public static native void glBufferSubData(int target, long offset, FloatBuffer data);
    public static native void glBufferSubData(int target, long offset, IntBuffer data);
    public static native void glGetBufferSubData(int target, long offset, ByteBuffer data);
    public static native int glGetBufferParameteri(int target, int name);
    public static native void glGetBufferParameter(int target, int name, IntBuffer out);
    public static native void glGenQueries(IntBuffer q);
    public static native int glGenQueries();
    public static native void glDeleteQueries(IntBuffer q);
    public static native void glDeleteQueries(int q);
    public static native boolean glIsQuery(int q);
    public static native void glBeginQuery(int target, int q);
    public static native void glEndQuery(int target);
    public static native void glGetQuery(int target, int name, IntBuffer out);
    public static native int glGetQueryi(int target, int name);
    public static native void glGetQueryObject(int q, int name, IntBuffer out);
    public static native int glGetQueryObjecti(int q, int name);
    public static native void glGetQueryObjectu(int q, int name, IntBuffer out);
    public static native int glGetQueryObjectui(int q, int name);

}
