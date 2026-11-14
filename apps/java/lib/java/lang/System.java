package java.lang;

import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintStream;
import java.util.Properties;

public final class System {
    public static InputStream in;
    public static PrintStream out, err;
    private static Properties props;
    private System() {}

    private static native String[] platformProperties();
    private static native String[] commandLineDefines();
    static native void write0(int fd, byte[] b, int off, int len);
    static native int readKey0();
    private static native void exit0(int status);
    private static native void gc0();

    static final class Console extends OutputStream {
        private final int fd;
        private final byte[] one = new byte[1];
        Console(int fd) { this.fd = fd; }
        public void write(int b) { one[0] = (byte) b; write0(fd, one, 0, 1); }
        public void write(byte[] b, int off, int len) {
            if (off < 0 || len < 0 || off > b.length - len) throw new IndexOutOfBoundsException();
            write0(fd, b, off, len);
        }
    }
    static final class Keyboard extends InputStream {
        public int read() throws IOException { int c = readKey0(); return c < 0 ? -1 : c & 0xff; }
    }

    static void initialize() {
        props = new Properties();
        String[] p = platformProperties();
        for (int i = 0; i + 1 < p.length; i += 2) props.setProperty(p[i], p[i + 1]);
        for (String d : commandLineDefines()) {
            int eq = d.indexOf('=');
            if (eq < 0) props.setProperty(d, "");
            else props.setProperty(d.substring(0, eq), d.substring(eq + 1));
        }
        out = new PrintStream(new Console(1), true);
        err = new PrintStream(new Console(2), true);
        in = new Keyboard();
    }

    public static native void arraycopy(Object src, int srcPos, Object dst, int dstPos, int length);
    public static native long currentTimeMillis();
    public static native long nanoTime();
    public static native int identityHashCode(Object o);
    public static void exit(int status) {
        if (getProperty("bjvm.trace.exit") != null) new Throwable("System.exit(" + status + ")").printStackTrace();
        Runtime.getRuntime().runShutdownHooks();
        exit0(status);
    }
    public static void gc() { gc0(); }
    public static void runFinalization() {}
    public static void load(String file) {}
    public static void loadLibrary(String name) {}
    public static String mapLibraryName(String name) { return "lib" + name + ".so"; }
    public static String lineSeparator() { return "\n"; }
    public static String getProperty(String key) { checkKey(key); return props.getProperty(key); }
    public static String getProperty(String key, String def) { checkKey(key); return props.getProperty(key, def); }
    public static String setProperty(String key, String value) { checkKey(key); return (String) props.setProperty(key, value); }
    public static String clearProperty(String key) { checkKey(key); return (String) props.remove(key); }
    public static Properties getProperties() { return props; }
    public static void setProperties(Properties p) { props = p != null ? p : new Properties(); }
    public static String getenv(String name) { if (name.equals("HOME")) return getProperty("user.home"); if (name.equals("USER")) return getProperty("user.name"); return null; }
    public static java.util.Map<String, String> getenv() { java.util.HashMap<String, String> m = new java.util.HashMap<String, String>(); m.put("HOME", getProperty("user.home")); m.put("USER", getProperty("user.name")); return m; }
    public static void setOut(PrintStream s) { out = s; }
    public static void setErr(PrintStream s) { err = s; }
    public static void setIn(InputStream s) { in = s; }
    public static SecurityManager getSecurityManager() { return null; }
    public static void setSecurityManager(SecurityManager s) {}
    private static void checkKey(String key) {
        if (key == null) throw new NullPointerException("key can't be null");
        if (key.length() == 0) throw new IllegalArgumentException("key can't be empty");
    }
}
