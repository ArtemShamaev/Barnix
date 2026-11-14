package java.io;

public abstract class Writer implements Appendable, Closeable, Flushable {
    protected Object lock;
    protected Writer() { lock = this; }
    protected Writer(Object lock) { this.lock = lock; }
    public void write(int c) throws IOException { write(new char[] {(char) c}, 0, 1); }
    public void write(char[] c) throws IOException { write(c, 0, c.length); }
    public abstract void write(char[] c, int off, int len) throws IOException;
    public void write(String s) throws IOException { write(s, 0, s.length()); }
    public void write(String s, int off, int len) throws IOException { char[] c = new char[len]; s.getChars(off, off + len, c, 0); write(c, 0, len); }
    public Writer append(CharSequence s) throws IOException { write(String.valueOf(s)); return this; }
    public Writer append(CharSequence s, int start, int end) throws IOException { write(String.valueOf(s == null ? "null" : s.subSequence(start, end))); return this; }
    public Writer append(char c) throws IOException { write(c); return this; }
    public abstract void flush() throws IOException;
    public abstract void close() throws IOException;
}
