package java.io;

public class FileOutputStream extends OutputStream {
    private final String path;
    private final byte[] buffer = new byte[65536];
    private int count;
    private boolean closed;

    static native boolean write0(String path, byte[] b, int off, int len, boolean append);

    public FileOutputStream(String name) throws FileNotFoundException { this(new File(name), false); }
    public FileOutputStream(String name, boolean append) throws FileNotFoundException { this(new File(name), append); }
    public FileOutputStream(File file) throws FileNotFoundException { this(file, false); }
    public FileOutputStream(File file, boolean append) throws FileNotFoundException {
        path = file.getPath();
        if (path.length() == 0 || file.isDirectory()) throw new FileNotFoundException(path + " (Is a directory)");
        if (!(append && file.exists()) && !write0(path, null, 0, 0, false))
            throw new FileNotFoundException(path + " (No such file or directory)");
    }
    public FileOutputStream(FileDescriptor fd) { path = null; closed = true; }
    private void drain() throws IOException {
        if (count == 0) return;
        if (!write0(path, buffer, 0, count, true)) throw new IOException("Write error: " + path);
        count = 0;
    }
    public void write(int b) throws IOException {
        if (closed) throw new IOException("Stream Closed");
        if (count == buffer.length) drain();
        buffer[count++] = (byte) b;
    }
    public void write(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (closed) throw new IOException("Stream Closed");
        if (len > buffer.length - count) {
            drain();
            if (len >= buffer.length) { if (!write0(path, b, off, len, true)) throw new IOException("Write error: " + path); return; }
        }
        System.arraycopy(b, off, buffer, count, len);
        count += len;
    }
    public void flush() throws IOException { if (!closed) drain(); }
    public void close() throws IOException { if (!closed) { drain(); closed = true; } }
    public final FileDescriptor getFD() { return new FileDescriptor(); }
}
