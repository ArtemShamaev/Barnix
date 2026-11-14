package java.io;

public class InputStreamReader extends Reader {
    private final InputStream in;
    private final boolean latin1;
    private final byte[] bytes = new byte[8192];
    private int pos, count;
    private int pendingLow = -1;

    public InputStreamReader(InputStream in) { super(in); this.in = in; latin1 = false; }
    public InputStreamReader(InputStream in, String charset) throws UnsupportedEncodingException {
        super(in);
        this.in = in;
        String c = charset.toUpperCase();
        if (c.equals("UTF-8") || c.equals("UTF8")) latin1 = false;
        else if (c.equals("ISO-8859-1") || c.equals("US-ASCII") || c.equals("ASCII") || c.equals("LATIN1") || c.equals("CP1252") || c.equals("WINDOWS-1252")) latin1 = true;
        else throw new UnsupportedEncodingException(charset);
    }
    public InputStreamReader(InputStream in, java.nio.charset.Charset cs) { this(in, cs, 0); }
    private InputStreamReader(InputStream in, java.nio.charset.Charset cs, int unused) { super(in); this.in = in; latin1 = !cs.name().toUpperCase().startsWith("UTF"); }
    public String getEncoding() { return latin1 ? "ISO8859_1" : "UTF8"; }
    private int next() throws IOException {
        if (pos >= count) { count = in.read(bytes, 0, bytes.length); pos = 0; if (count <= 0) { count = 0; return -1; } }
        return bytes[pos++] & 0xff;
    }
    private int decode() throws IOException {
        if (pendingLow >= 0) { int c = pendingLow; pendingLow = -1; return c; }
        int c = next();
        if (c < 0x80 || latin1) return c;
        int need, cp;
        if (c >= 0xf0) { need = 3; cp = c & 7; } else if (c >= 0xe0) { need = 2; cp = c & 15; } else if (c >= 0xc0) { need = 1; cp = c & 31; } else return 0xfffd;
        for (int i = 0; i < need; i++) { int d = next(); if (d < 0 || (d & 0xc0) != 0x80) return 0xfffd; cp = cp << 6 | (d & 0x3f); }
        if (cp >= 0x10000) { cp -= 0x10000; pendingLow = 0xdc00 + (cp & 0x3ff); return 0xd800 + (cp >> 10); }
        return cp;
    }
    public int read() throws IOException { return decode(); }
    public int read(char[] c, int off, int len) throws IOException {
        if (len == 0) return 0;
        int n = 0;
        while (n < len) {
            int x = decode();
            if (x < 0) break;
            c[off + n++] = (char) x;
            if (pos >= count && pendingLow < 0 && in.available() <= 0) break;
        }
        return n == 0 ? -1 : n;
    }
    public boolean ready() throws IOException { return pos < count || in.available() > 0; }
    public void close() throws IOException { in.close(); }
}
