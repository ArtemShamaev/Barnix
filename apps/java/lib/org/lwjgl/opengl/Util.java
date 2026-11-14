package org.lwjgl.opengl;

public final class Util {
    private Util() {}
    public static void checkGLError() { int e = GL11.glGetError(); if (e != 0) throw new OpenGLException(translateGLErrorString(e)); }
    public static String translateGLErrorString(int e) { return org.lwjgl.util.glu.GLU.gluErrorString(e); }
}
