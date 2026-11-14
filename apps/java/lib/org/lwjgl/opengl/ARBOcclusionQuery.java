package org.lwjgl.opengl;

import java.nio.*;

public final class ARBOcclusionQuery {
    private ARBOcclusionQuery() {}
    public static final int GL_QUERY_COUNTER_BITS_ARB = 0x8864;
    public static final int GL_CURRENT_QUERY_ARB = 0x8865;
    public static final int GL_QUERY_RESULT_ARB = 0x8866;
    public static final int GL_QUERY_RESULT_AVAILABLE_ARB = 0x8867;
    public static final int GL_SAMPLES_PASSED_ARB = 0x8914;
    public static native void glGenQueriesARB(IntBuffer q);
    public static native int glGenQueriesARB();
    public static native void glDeleteQueriesARB(IntBuffer q);
    public static native void glDeleteQueriesARB(int q);
    public static native boolean glIsQueryARB(int q);
    public static native void glBeginQueryARB(int target, int q);
    public static native void glEndQueryARB(int target);
    public static native void glGetQueryARB(int target, int name, IntBuffer out);
    public static native int glGetQueryiARB(int target, int name);
    public static native void glGetQueryObjectARB(int q, int name, IntBuffer out);
    public static native int glGetQueryObjectiARB(int q, int name);
    public static native void glGetQueryObjectuARB(int q, int name, IntBuffer out);
    public static native int glGetQueryObjectuiARB(int q, int name);

}
