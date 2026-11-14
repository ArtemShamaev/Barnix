package org.lwjgl.input;

import org.lwjgl.LWJGLException;

/* Mouse state and events from the Barnix display (Display.update fills them). */
public final class Mouse {
    private static boolean created, grabbed;
    private static final int[] state = new int[6];
    private static final int[] events = new int[64 * 5];
    private static int count, index = -1;
    private static int x, y, dx, dy, dwheel, buttons;
    private static int eventButton, eventWheel, eventX, eventY, lastEventX, lastEventY;
    private static boolean eventState;
    private static Cursor cursor;

    private Mouse() {}
    private static native int read0(int[] out);
    private static native void state0(int[] out);
    private static native void grab0(boolean grab);
    private static native void setPosition0(int x, int y);

    public static void create() throws LWJGLException { created = true; }
    public static void autoCreate() { created = true; }
    public static boolean isCreated() { return created; }
    public static void destroy() { created = false; }
    public static void poll() { update(); }
    private static void update() {
        state0(state);
        x = state[0]; y = state[1]; dx += state[2]; dy += state[3]; dwheel += state[4]; buttons = state[5];
    }
    public static boolean next() {
        if (index + 1 >= count) { count = read0(events); index = -1; if (count == 0) return false; }
        index++;
        int[] e = events;
        int i = index * 5;
        eventButton = e[i]; eventState = e[i + 1] != 0; eventWheel = e[i + 2];
        lastEventX = eventX; lastEventY = eventY;
        eventX = e[i + 3]; eventY = e[i + 4];
        return true;
    }
    public static int getEventButton() { return eventButton; }
    public static boolean getEventButtonState() { return eventState; }
    public static int getEventDWheel() { return eventWheel; }
    public static int getEventX() { return eventX; }
    public static int getEventY() { return eventY; }
    public static int getEventDX() { return eventX - lastEventX; }
    public static int getEventDY() { return eventY - lastEventY; }
    public static long getEventNanoseconds() { return System.nanoTime(); }
    public static int getX() { update(); return x; }
    public static int getY() { update(); return y; }
    public static int getDX() { update(); int r = dx; dx = 0; return r; }
    public static int getDY() { update(); int r = dy; dy = 0; return r; }
    public static int getDWheel() { update(); int r = dwheel; dwheel = 0; return r; }
    public static boolean isButtonDown(int b) { update(); return b >= 0 && b < 3 && (buttons & (1 << b)) != 0; }
    public static int getButtonCount() { return 3; }
    public static String getButtonName(int b) { return b >= 0 && b < 3 ? "BUTTON" + b : null; }
    public static int getButtonIndex(String n) { return n.startsWith("BUTTON") ? Integer.parseInt(n.substring(6)) : -1; }
    public static boolean hasWheel() { return true; }
    public static boolean isGrabbed() { return grabbed; }
    public static void setGrabbed(boolean g) { grabbed = g; grab0(g); dx = dy = 0; }
    public static void setCursorPosition(int nx, int ny) { setPosition0(nx, ny); x = nx; y = ny; }
    public static boolean isInsideWindow() { return true; }
    public static void setClipMouseCoordinatesToWindow(boolean c) {}
    public static Cursor getNativeCursor() { return cursor; }
    public static Cursor setNativeCursor(Cursor c) throws LWJGLException { Cursor old = cursor; cursor = c; return old; }
    public static void updateCursor() {}
}
