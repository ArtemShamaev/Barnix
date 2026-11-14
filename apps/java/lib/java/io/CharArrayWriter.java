package java.io;

public class CharArrayWriter extends Writer {
    private final StringBuilder buf = new StringBuilder();
    public CharArrayWriter() {}
    public CharArrayWriter(int size) {}
    public void write(int c) { buf.append((char) c); }
    public void write(char[] c, int off, int len) { buf.append(c, off, len); }
    public void write(String s, int off, int len) { buf.append(s, off, off + len); }
    public char[] toCharArray() { return buf.toString().toCharArray(); }
    public int size() { return buf.length(); }
    public void reset() { buf.setLength(0); }
    public String toString() { return buf.toString(); }
    public void flush() {}
    public void close() {}
}
