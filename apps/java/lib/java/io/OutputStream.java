package java.io;

public abstract class OutputStream implements Closeable, Flushable {
    public OutputStream() {}
    public abstract void write(int b) throws IOException;
    public void write(byte[] b) throws IOException { write(b, 0, b.length); }
    public void write(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        for (int i = 0; i < len; i++) write(b[off + i]);
    }
    public void flush() throws IOException {}
    public void close() throws IOException {}
}
