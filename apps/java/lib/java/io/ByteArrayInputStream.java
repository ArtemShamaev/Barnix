package java.io;

public class ByteArrayInputStream extends InputStream {
    protected byte[] buf;
    protected int pos, mark, count;
    public ByteArrayInputStream(byte[] buf) { this.buf = buf; count = buf.length; }
    public ByteArrayInputStream(byte[] buf, int offset, int length) { this.buf = buf; pos = offset; count = Math.min(offset + length, buf.length); mark = offset; }
    public synchronized int read() { return pos < count ? buf[pos++] & 0xff : -1; }
    public synchronized int read(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (pos >= count) return -1;
        int n = Math.min(len, count - pos);
        System.arraycopy(buf, pos, b, off, n);
        pos += n;
        return n;
    }
    public synchronized long skip(long n) { long k = Math.min(count - pos, Math.max(0, n)); pos += k; return k; }
    public synchronized int available() { return count - pos; }
    public boolean markSupported() { return true; }
    public void mark(int limit) { mark = pos; }
    public synchronized void reset() { pos = mark; }
    public void close() {}
}
