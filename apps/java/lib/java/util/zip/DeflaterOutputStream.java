package java.util.zip;

import java.io.*;

public class DeflaterOutputStream extends FilterOutputStream {
    protected Deflater def;
    protected byte[] buf;
    private boolean finished, closed;
    public DeflaterOutputStream(OutputStream out) { this(out, new Deflater(), 512); }
    public DeflaterOutputStream(OutputStream out, Deflater def) { this(out, def, 512); }
    public DeflaterOutputStream(OutputStream out, Deflater def, int size) { super(out); this.def = def; buf = new byte[size]; }
    public DeflaterOutputStream(OutputStream out, boolean syncFlush) { this(out); }
    public void write(int b) throws IOException { write(new byte[] {(byte) b}, 0, 1); }
    public void write(byte[] b, int off, int len) throws IOException { if (finished) throw new IOException("write beyond end of stream"); def.setInput(b, off, len); }
    public void finish() throws IOException {
        if (finished) return;
        finished = true;
        def.finish();
        byte[] chunk = new byte[65536];
        while (!def.finished()) { int n = def.deflate(chunk, 0, chunk.length); out.write(chunk, 0, n); }
    }
    public void flush() throws IOException { out.flush(); }
    public void close() throws IOException { if (!closed) { finish(); out.close(); closed = true; } }
}
