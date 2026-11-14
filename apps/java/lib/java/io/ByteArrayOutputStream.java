package java.io;

public class ByteArrayOutputStream extends OutputStream {
    protected byte[] buf;
    protected int count;
    public ByteArrayOutputStream() { this(32); }
    public ByteArrayOutputStream(int size) { if (size < 0) throw new IllegalArgumentException("Negative initial size: " + size); buf = new byte[size]; }
    private void ensure(int n) { if (n > buf.length) buf = java.util.Arrays.copyOf(buf, Math.max(buf.length * 2, n)); }
    public synchronized void write(int b) { ensure(count + 1); buf[count++] = (byte) b; }
    public synchronized void write(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        ensure(count + len);
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }
    public void writeBytes(byte[] b) { write(b, 0, b.length); }
    public synchronized void writeTo(OutputStream out) throws IOException { out.write(buf, 0, count); }
    public synchronized void reset() { count = 0; }
    public synchronized byte[] toByteArray() { return java.util.Arrays.copyOf(buf, count); }
    public synchronized int size() { return count; }
    public synchronized String toString() { return new String(buf, 0, count); }
    public synchronized String toString(String charset) throws UnsupportedEncodingException { return new String(buf, 0, count, charset); }
    public void close() {}
}
