package java.io;

public class BufferedInputStream extends FilterInputStream {
    protected volatile byte[] buf;
    protected int count, pos, markpos = -1, marklimit;
    public BufferedInputStream(InputStream in) { this(in, 8192); }
    public BufferedInputStream(InputStream in, int size) { super(in); if (size <= 0) throw new IllegalArgumentException("Buffer size <= 0"); buf = new byte[size]; }
    private void fill() throws IOException {
        if (markpos < 0) pos = 0;
        else if (pos >= buf.length) {
            if (markpos > 0) { int keep = pos - markpos; System.arraycopy(buf, markpos, buf, 0, keep); pos = keep; markpos = 0; }
            else if (buf.length >= marklimit) { markpos = -1; pos = 0; }
            else { byte[] n = new byte[Math.min(buf.length * 2, marklimit)]; System.arraycopy(buf, 0, n, 0, pos); buf = n; }
        }
        count = pos;
        int n = in.read(buf, pos, buf.length - pos);
        if (n > 0) count = n + pos;
    }
    public synchronized int read() throws IOException {
        if (pos >= count) { fill(); if (pos >= count) return -1; }
        return buf[pos++] & 0xff;
    }
    public synchronized int read(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (len == 0) return 0;
        int avail = count - pos;
        if (avail <= 0) {
            if (len >= buf.length && markpos < 0) return in.read(b, off, len);
            fill();
            avail = count - pos;
            if (avail <= 0) return -1;
        }
        int n = Math.min(avail, len);
        System.arraycopy(buf, pos, b, off, n);
        pos += n;
        if (n < len && in.available() > 0) { int more = in.read(b, off + n, len - n); if (more > 0) n += more; }
        return n;
    }
    public synchronized long skip(long n) throws IOException {
        if (n <= 0) return 0;
        long avail = count - pos;
        if (avail <= 0) { if (markpos < 0) return in.skip(n); fill(); avail = count - pos; if (avail <= 0) return 0; }
        long s = Math.min(avail, n);
        pos += s;
        return s;
    }
    public synchronized int available() throws IOException { return (count - pos) + in.available(); }
    public synchronized void mark(int limit) { marklimit = limit; markpos = pos; }
    public synchronized void reset() throws IOException { if (markpos < 0) throw new IOException("Resetting to invalid mark"); pos = markpos; }
    public boolean markSupported() { return true; }
}
