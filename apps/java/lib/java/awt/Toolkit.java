package java.awt;

import java.awt.datatransfer.Clipboard;

public class Toolkit {
    private static final Toolkit instance = new Toolkit();
    private final Clipboard clipboard = new Clipboard("System");
    protected Toolkit() {}
    public static Toolkit getDefaultToolkit() { return instance; }
    public Clipboard getSystemClipboard() { return clipboard; }
    public Dimension getScreenSize() { return new Dimension(org.lwjgl.opengl.Display.getDesktopDisplayMode().getWidth(), org.lwjgl.opengl.Display.getDesktopDisplayMode().getHeight()); }
    public int getScreenResolution() { return 96; }
    public void sync() {}
    public void beep() {}
}
