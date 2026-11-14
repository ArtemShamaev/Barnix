package org.lwjgl;

public final class Sys {
    private Sys() {}
    public static String getVersion() { return "2.9.4 (Barnix BJVM)"; }
    public static long getTimerResolution() { return 1000; }
    public static long getTime() { return System.currentTimeMillis(); }
    public static boolean is64Bit() { return false; }
    public static void initialize() {}
    public static void alert(String title, String message) { System.err.println(title + ": " + message); }
    public static boolean openURL(String url) { System.out.println("Open in a browser: " + url); return false; }
    public static String getClipboard() { return null; }
}
