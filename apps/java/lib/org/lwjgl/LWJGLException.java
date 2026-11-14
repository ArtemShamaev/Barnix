package org.lwjgl;

public class LWJGLException extends Exception {
    public LWJGLException() {}
    public LWJGLException(String s) { super(s); }
    public LWJGLException(String s, Throwable cause) { super(s, cause); }
    public LWJGLException(Throwable cause) { super(cause); }
}
