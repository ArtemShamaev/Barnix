package java.io;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;
    private final boolean latin1;
    public OutputStreamWriter(OutputStream out) { super(out); this.out = out; latin1 = false; }
    public OutputStreamWriter(OutputStream out, String charset) throws UnsupportedEncodingException {
        super(out);
        this.out = out;
        String c = charset.toUpperCase();
        if (c.equals("UTF-8") || c.equals("UTF8")) latin1 = false;
        else if (c.equals("ISO-8859-1") || c.equals("US-ASCII") || c.equals("ASCII") || c.equals("LATIN1")) latin1 = true;
        else throw new UnsupportedEncodingException(charset);
    }
    public OutputStreamWriter(OutputStream out, java.nio.charset.Charset cs) { super(out); this.out = out; latin1 = !cs.name().toUpperCase().startsWith("UTF"); }
    public String getEncoding() { return latin1 ? "ISO8859_1" : "UTF8"; }
    public void write(char[] c, int off, int len) throws IOException {
        String s = new String(c, off, len);
        out.write(latin1 ? s.getBytes("ISO-8859-1") : s.getBytes());
    }
    public void write(String s, int off, int len) throws IOException { String t = s.substring(off, off + len); out.write(latin1 ? t.getBytes("ISO-8859-1") : t.getBytes()); }
    public void flush() throws IOException { out.flush(); }
    public void close() throws IOException { out.close(); }
}
