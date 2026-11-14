package java.io;

public abstract class InputStream implements Closeable {
    public InputStream() {}
    public abstract int read() throws IOException;
    public int read(byte[] b) throws IOException { return read(b, 0, b.length); }
    public int read(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (len == 0) return 0;
        int c = read();
        if (c == -1) return -1;
        b[off] = (byte) c;
        int i = 1;
        try {
            for (; i < len; i++) {
                if (available() <= 0 && i > 0) break;
                c = read();
                if (c == -1) break;
                b[off + i] = (byte) c;
            }
        } catch (IOException e) {}
        return i;
    }
    public long skip(long n) throws IOException {
        long left = n;
        byte[] buf = new byte[(int) Math.min(2048, Math.max(n, 1))];
        while (left > 0) { int r = read(buf, 0, (int) Math.min(buf.length, left)); if (r < 0) break; left -= r; }
        return n - left;
    }
    public int available() throws IOException { return 0; }
    public void close() throws IOException {}
    public synchronized void mark(int limit) {}
    public synchronized void reset() throws IOException { throw new IOException("mark/reset not supported"); }
    public boolean markSupported() { return false; }
    public byte[] readAllBytes() throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int n;
        while ((n = read(b, 0, b.length)) > 0) o.write(b, 0, n);
        return o.toByteArray();
    }
    public long transferTo(OutputStream out) throws IOException {
        byte[] b = new byte[8192];
        long total = 0;
        int n;
        while ((n = read(b, 0, b.length)) > 0) { out.write(b, 0, n); total += n; }
        return total;
    }
}
