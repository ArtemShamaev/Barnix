package java.io;

public class PushbackInputStream extends FilterInputStream {
    protected byte[] buf;
    protected int pos;
    public PushbackInputStream(InputStream in) { this(in, 1); }
    public PushbackInputStream(InputStream in, int size) { super(in); buf = new byte[size]; pos = size; }
    public int read() throws IOException { return pos < buf.length ? buf[pos++] & 0xff : super.read(); }
    public int read(byte[] b, int off, int len) throws IOException {
        if (len == 0) return 0;
        int n = 0;
        while (pos < buf.length && n < len) b[off + n++] = buf[pos++];
        if (n < len) { int k = super.read(b, off + n, len - n); if (k > 0) n += k; else if (n == 0) return -1; }
        return n;
    }
    public void unread(int b) throws IOException { if (pos == 0) throw new IOException("Push back buffer is full"); buf[--pos] = (byte) b; }
    public void unread(byte[] b, int off, int len) throws IOException { for (int i = len - 1; i >= 0; i--) unread(b[off + i]); }
    public void unread(byte[] b) throws IOException { unread(b, 0, b.length); }
    public int available() throws IOException { return buf.length - pos + super.available(); }
}
