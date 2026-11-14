package org.lwjgl.input;

import org.lwjgl.LWJGLException;

/* No game controllers. */
public final class Controllers {
    private Controllers() {}
    public static void create() throws LWJGLException {}
    public static boolean isCreated() { return true; }
    public static int getControllerCount() { return 0; }
    public static boolean next() { return false; }
    public static void poll() {}
    public static void destroy() {}
}
