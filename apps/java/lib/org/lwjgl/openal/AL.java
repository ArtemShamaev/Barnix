package org.lwjgl.openal;

import org.lwjgl.LWJGLException;

/* No OpenAL on Barnix yet: create() fails and programs run without sound. */
public final class AL {
    private AL() {}
    public static void create() throws LWJGLException { throw new LWJGLException("OpenAL is not available on Barnix"); }
    public static void create(String device, int freq, int refresh, boolean sync) throws LWJGLException { create(); }
    public static boolean isCreated() { return false; }
    public static void destroy() {}
}
