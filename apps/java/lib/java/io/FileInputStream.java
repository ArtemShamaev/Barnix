package java.io;

public class FileInputStream extends InputStream {
    private final String path;
    private long position;
    private boolean closed;
    private final byte[] buffer = new byte[16384];
    private int bufferStart, bufferCount;

    static native int read0(String path, long position, byte[] b, int off, int len);

    public FileInputStream(String name) throws FileNotFoundException { this(new File(name)); }
    public FileInputStream(File file) throws FileNotFoundException {
        String p = file.getPath();
        if (p.length() == 0 || !file.exists()) throw new FileNotFoundException(p + " (No such file or directory)");
        if (file.isDirectory()) throw new FileNotFoundException(p + " (Is a directory)");
        path = p;
    }
    public FileInputStream(FileDescriptor fd) { path = "/dev/stdin"; closed = true; }
    private boolean fill() throws IOException {
        if (closed) throw new IOException("Stream Closed");
        bufferStart = 0;
        bufferCount = read0(path, position, buffer, 0, buffer.length);
        if (bufferCount <= 0) { bufferCount = 0; return false; }
        return true;
    }
    public int read() throws IOException {
        if (bufferStart >= bufferCount && !fill()) return -1;
        position++;
        return buffer[bufferStart++] & 0xff;
    }
    public int read(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (len == 0) return 0;
        int avail = bufferCount - bufferStart;
        if (avail > 0) {
            int n = Math.min(avail, len);
            System.arraycopy(buffer, bufferStart, b, off, n);
            bufferStart += n;
            position += n;
            return n;
        }
        if (len >= buffer.length) {
            if (closed) throw new IOException("Stream Closed");
            int n = read0(path, position, b, off, len);
            if (n > 0) position += n;
            return n <= 0 ? -1 : n;
        }
        if (!fill()) return -1;
        return read(b, off, len);
    }
    public long skip(long n) throws IOException {
        if (n <= 0) return 0;
        long avail = bufferCount - bufferStart;
        if (n <= avail) { bufferStart += (int) n; position += n; return n; }
        long size = new File(path).length();
        long target = Math.min(size, position + n), done = target - position;
        bufferStart = bufferCount = 0;
        position = target;
        return done;
    }
    public int available() throws IOException { return (int) Math.max(0, Math.min(Integer.MAX_VALUE, new File(path).length() - position)); }
    public void close() { closed = true; }
    public final FileDescriptor getFD() { return new FileDescriptor(); }
}
