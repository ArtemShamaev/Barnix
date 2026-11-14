package java.io;

public class StringWriter extends Writer {
    private final StringBuffer buf;
    public StringWriter() { buf = new StringBuffer(); lock = buf; }
    public StringWriter(int size) { buf = new StringBuffer(size); lock = buf; }
    public void write(int c) { buf.append((char) c); }
    public void write(char[] c, int off, int len) { buf.append(c, off, len); }
    public void write(String s) { buf.append(s); }
    public void write(String s, int off, int len) { buf.append(s, off, off + len); }
    public StringWriter append(CharSequence s) { buf.append(String.valueOf(s)); return this; }
    public StringWriter append(char c) { buf.append(c); return this; }
    public StringBuffer getBuffer() { return buf; }
    public String toString() { return buf.toString(); }
    public void flush() {}
    public void close() {}
}
