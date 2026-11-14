package org.lwjgl.util.glu;

import java.nio.*;

public class GLU {
    public static native void gluPerspective(float fovy, float aspect, float zNear, float zFar);
    public static native void gluLookAt(float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz);
    public static native void gluOrtho2D(float left, float right, float bottom, float top);
    public static native void gluPickMatrix(float x, float y, float w, float h, IntBuffer viewport);
    public static native int gluBuild2DMipmaps(int target, int components, int width, int height, int format, int type, ByteBuffer data);
    public static String gluErrorString(int e) {
        switch (e) {
        case 0: return "No error";
        case 0x0500: return "Invalid enum";
        case 0x0501: return "Invalid value";
        case 0x0502: return "Invalid operation";
        case 0x0503: return "Stack overflow";
        case 0x0504: return "Stack underflow";
        case 0x0505: return "Out of memory";
        default: return null;
        }
    }
}
