package org.lwjgl.opengl;

import org.lwjgl.LWJGLException;

/* The whole Barnix screen, through Barnix OpenGL. */
public final class Display {
    private static DisplayMode mode, desktop;
    private static boolean created, fullscreen, vsync, resizable;
    private static String title = "";
    private static java.awt.Canvas parent;
    private static int lastWidth, lastHeight;
    private static boolean resized;

    private Display() {}
    private static native int open0(int width, int height);
    private static native void swap0();
    private static native void close0();
    private static native int screenWidth0();
    private static native int screenHeight0();
    static native void poll0();
    private static native int width0();
    private static native int height0();

    public static DisplayMode getDesktopDisplayMode() {
        if (desktop == null) desktop = new DisplayMode(screenWidth0(), screenHeight0(), 32, 60, true);
        return desktop;
    }
    public static DisplayMode[] getAvailableDisplayModes() { return new DisplayMode[] {getDesktopDisplayMode(), new DisplayMode(854, 480), new DisplayMode(640, 480), new DisplayMode(800, 600)}; }
    public static DisplayMode getDisplayMode() {
        if (mode == null) {
            if (parent != null && parent.getWidth() > 0) mode = new DisplayMode(parent.getWidth(), parent.getHeight());
            else mode = new DisplayMode(854, 480);
        }
        return mode;
    }
    public static void setDisplayMode(DisplayMode m) throws LWJGLException {
        if (m == null) throw new NullPointerException("mode must be non-null");
        mode = m;
        if (created) reopen();
    }
    public static void setDisplayModeAndFullscreen(DisplayMode m) throws LWJGLException { fullscreen = true; setDisplayMode(m); }
    public static void setFullscreen(boolean f) throws LWJGLException {
        if (f == fullscreen) return;
        fullscreen = f;
        if (created) reopen();
    }
    public static boolean isFullscreen() { return fullscreen; }
    public static void setTitle(String t) { title = t == null ? "" : t; }
    public static String getTitle() { return title; }
    public static void setVSyncEnabled(boolean v) { vsync = v; }
    public static void setSwapInterval(int i) {}
    public static void setResizable(boolean r) { resizable = r; }
    public static boolean isResizable() { return resizable; }
    public static boolean wasResized() { boolean r = resized; resized = false; return r; }
    public static void setParent(java.awt.Canvas c) { parent = c; }
    public static java.awt.Canvas getParent() { return parent; }
    public static void setIcon(java.nio.ByteBuffer[] icons) {}
    public static void setLocation(int x, int y) {}
    public static void setInitialBackground(float r, float g, float b) {}
    public static int getX() { return 0; }
    public static int getY() { return 0; }
    public static int getWidth() { return created ? width0() : getDisplayMode().getWidth(); }
    public static int getHeight() { return created ? height0() : getDisplayMode().getHeight(); }
    public static String getAdapter() { return "Barnix SoftGL"; }
    public static String getVersion() { return "BJVM"; }
    public static void setDisplayConfiguration(float gamma, float brightness, float contrast) {}

    private static void reopen() throws LWJGLException {
        DisplayMode m = fullscreen ? getDesktopDisplayMode() : getDisplayMode();
        String size = System.getProperty("bjvm.display");
        int w = m.getWidth(), h = m.getHeight();
        if (size != null) { int x = size.indexOf('x'); if (x > 0) { w = Integer.parseInt(size.substring(0, x)); h = Integer.parseInt(size.substring(x + 1)); } }
        if (open0(w, h) != 0) throw new LWJGLException("Barnix OpenGL could not open a " + w + "x" + h + " display");
        if (created && (lastWidth != width0() || lastHeight != height0())) resized = true;
        lastWidth = width0(); lastHeight = height0();
        if (parent != null) parent.setSize(lastWidth, lastHeight);
        created = true;
    }
    public static void create() throws LWJGLException {
        if (created) throw new IllegalStateException("Only one LWJGL context may be instantiated at any one time.");
        reopen();
        org.lwjgl.input.Keyboard.autoCreate();
        org.lwjgl.input.Mouse.autoCreate();
    }
    public static void create(PixelFormat f) throws LWJGLException { create(); }
    public static void create(PixelFormat f, ContextAttribs a) throws LWJGLException { create(); }
    public static boolean isCreated() { return created; }
    public static boolean isCurrent() { return created; }
    public static void makeCurrent() {}
    public static void releaseContext() {}
    public static void destroy() { if (created) { close0(); created = false; } }
    public static boolean isCloseRequested() { return org.lwjgl.input.Keyboard.closeRequested(); }
    public static boolean isActive() { return true; }
    public static boolean isVisible() { return true; }
    public static boolean isDirty() { return false; }
    public static void processMessages() { poll0(); }
    public static void swapBuffers() throws LWJGLException { swap0(); }
    public static void update() { update(true); }
    public static void update(boolean processMessages) { if (created) swap0(); if (processMessages) poll0(); }
    public static void sync(int fps) {
        long frame = 1000L / Math.max(1, fps), now = System.currentTimeMillis();
        long wait = nextFrame - now;
        if (wait > 0) { try { Thread.sleep(wait); } catch (InterruptedException e) {} }
        nextFrame = Math.max(nextFrame + frame, now);
    }
    private static long nextFrame;
}
