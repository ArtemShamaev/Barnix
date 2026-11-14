package java.nio.charset;

public abstract class Charset implements Comparable<Charset> {
    private final String name;
    protected Charset(String name, String[] aliases) { this.name = name; }
    private static final class Simple extends Charset { Simple(String n) { super(n, null); } }
    public static Charset forName(String name) {
        String n = name.toUpperCase();
        if (n.equals("UTF-8") || n.equals("UTF8")) return StandardCharsets.UTF_8;
        if (n.equals("ISO-8859-1") || n.equals("LATIN1")) return StandardCharsets.ISO_8859_1;
        if (n.equals("US-ASCII") || n.equals("ASCII")) return StandardCharsets.US_ASCII;
        if (n.startsWith("UTF-16")) return new Simple(n);
        throw new UnsupportedCharsetException(name);
    }
    static Charset make(String n) { return new Simple(n); }
    public static Charset defaultCharset() { return StandardCharsets.UTF_8; }
    public static boolean isSupported(String name) { try { forName(name); return true; } catch (RuntimeException e) { return false; } }
    public final String name() { return name; }
    public String displayName() { return name; }
    public final int compareTo(Charset o) { return name.compareToIgnoreCase(o.name); }
    public final String toString() { return name; }
}
