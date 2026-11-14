package org.lwjgl.opengl;

public final class PixelFormat {
    public PixelFormat() {}
    public PixelFormat(int alpha, int depth, int stencil) {}
    public PixelFormat(int alpha, int depth, int stencil, int samples) {}
    public PixelFormat withDepthBits(int d) { return this; }
    public PixelFormat withSamples(int s) { return this; }
    public PixelFormat withStencilBits(int s) { return this; }
    public PixelFormat withAlphaBits(int a) { return this; }
}
