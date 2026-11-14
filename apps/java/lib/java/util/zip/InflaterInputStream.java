package java.util.zip;

import java.io.*;

public class InflaterInputStream extends FilterInputStream {
    protected Inflater inf;
    protected byte[] buf;
    protected int len;
    private boolean loaded, closed;

    public InflaterInputStream(InputStream in) { this(in, new Inflater(), 512); }
    public InflaterInputStream(InputStream in, Inflater inf) { this(in, inf, 512); }
    public InflaterInputStream(InputStream in, Inflater inf, int size) { super(in); this.inf = inf; buf = new byte[size]; }
    /* All compressed input at once, then one decode. */
    protected void load() throws IOException {
        if (loaded) return;
        loaded = true;
        ByteArrayOutputStream all = new ByteArrayOutputStream();
        byte[] b = new byte[16384];
        int n;
        while ((n = in.read(b, 0, b.length)) > 0) all.write(b, 0, n);
        byte[] data = all.toByteArray();
        inf.setInput(data, 0, data.length);
    }
    protected void fill() throws IOException { load(); }
    public int read() throws IOException { byte[] one = new byte[1]; return read(one, 0, 1) <= 0 ? -1 : one[0] & 0xff; }
    public int read(byte[] b, int off, int n) throws IOException {
        if (closed) throw new IOException("Stream closed");
        if (off < 0 || n < 0 || n > b.length - off) throw new IndexOutOfBoundsException();
        if (n == 0) return 0;
        load();
        try {
            if (inf.finished()) return -1;
            int k = inf.inflate(b, off, n);
            if (k == 0 && inf.needsInput()) throw new EOFException("Unexpected end of ZLIB input stream");
            return k == 0 && inf.finished() ? -1 : k;
        } catch (DataFormatException e) { throw new ZipException(e.getMessage() == null ? "Invalid ZLIB data format" : e.getMessage()); }
    }
    public int available() throws IOException { return closed || inf.finished() ? 0 : 1; }
    public long skip(long n) throws IOException { byte[] b = new byte[512]; long left = n; while (left > 0) { int k = read(b, 0, (int) Math.min(b.length, left)); if (k < 0) break; left -= k; } return n - left; }
    public void close() throws IOException { if (!closed) { inf.end(); in.close(); closed = true; } }
    public boolean markSupported() { return false; }
}
