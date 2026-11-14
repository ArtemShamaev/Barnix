package java.util.zip;

import java.io.*;

public class GZIPOutputStream extends DeflaterOutputStream {
    protected CRC32 crc = new CRC32();
    private long size;
    private boolean done;
    public GZIPOutputStream(OutputStream out) throws IOException { this(out, 512); }
    public GZIPOutputStream(OutputStream out, int size) throws IOException {
        super(out, new Deflater(Deflater.DEFAULT_COMPRESSION, true), size);
        out.write(new byte[] {0x1f, (byte) 0x8b, 8, 0, 0, 0, 0, 0, 0, (byte) 0xff});
    }
    public GZIPOutputStream(OutputStream out, boolean syncFlush) throws IOException { this(out); }
    public void write(byte[] b, int off, int len) throws IOException { super.write(b, off, len); crc.update(b, off, len); size += len; }
    public void finish() throws IOException {
        if (done) return;
        done = true;
        super.finish();
        long c = crc.getValue();
        byte[] t = new byte[8];
        for (int i = 0; i < 4; i++) { t[i] = (byte) (c >> (8 * i)); t[4 + i] = (byte) (size >> (8 * i)); }
        out.write(t);
    }
}
