package org.lwjgl.opengl;

public final class GLContext {
    private static ContextCapabilities caps;
    private GLContext() {}
    public static ContextCapabilities getCapabilities() { if (caps == null) caps = new ContextCapabilities(); return caps; }
    public static void useContext(Object context) {}
    public static void loadOpenGLLibrary() {}
}
