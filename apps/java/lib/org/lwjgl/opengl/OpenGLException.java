package org.lwjgl.opengl;

public class OpenGLException extends RuntimeException {
    public OpenGLException(int error) { super("OpenGL error " + error); }
    public OpenGLException(String s) { super(s); }
}
