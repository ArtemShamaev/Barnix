package java.io;

public abstract class Reader implements Closeable {
    protected Object lock;
    protected Reader() { lock = this; }
    protected Reader(Object lock) { this.lock = lock; }
    public int read() throws IOException { char[] c = new char[1]; return read(c, 0, 1) == -1 ? -1 : c[0]; }
    public int read(char[] c) throws IOException { return read(c, 0, c.length); }
    public abstract int read(char[] c, int off, int len) throws IOException;
    public long skip(long n) throws IOException { char[] b = new char[512]; long left = n; while (left > 0) { int r = read(b, 0, (int) Math.min(b.length, left)); if (r < 0) break; left -= r; } return n - left; }
    public boolean ready() throws IOException { return false; }
    public boolean markSupported() { return false; }
    public void mark(int limit) throws IOException { throw new IOException("mark() not supported"); }
    public void reset() throws IOException { throw new IOException("reset() not supported"); }
    public abstract void close() throws IOException;
}
