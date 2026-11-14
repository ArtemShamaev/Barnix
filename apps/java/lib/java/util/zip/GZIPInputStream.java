package java.util.zip;

import java.io.*;

public class GZIPInputStream extends InflaterInputStream {
    public static final int GZIP_MAGIC = 0x8b1f;
    protected CRC32 crc = new CRC32();
    protected boolean eos;
    public GZIPInputStream(InputStream in) throws IOException { this(in, 512); }
    public GZIPInputStream(InputStream in, int size) throws IOException {
        super(in, new Inflater(true), size);
        DataInputStream d = new DataInputStream(in);
        int magic = d.readUnsignedByte() | d.readUnsignedByte() << 8;
        if (magic != GZIP_MAGIC) throw new ZipException("Not in GZIP format");
        if (d.readUnsignedByte() != 8) throw new ZipException("Unsupported compression method");
        int flags = d.readUnsignedByte();
        d.skipBytes(6);
        if ((flags & 4) != 0) { int n = d.readUnsignedByte() | d.readUnsignedByte() << 8; d.skipBytes(n); }
        if ((flags & 8) != 0) while (d.readUnsignedByte() != 0) {}
        if ((flags & 16) != 0) while (d.readUnsignedByte() != 0) {}
        if ((flags & 2) != 0) d.skipBytes(2);
    }
    public int read(byte[] b, int off, int n) throws IOException {
        if (eos) return -1;
        int k = super.read(b, off, n);
        if (k < 0) eos = true; else crc.update(b, off, k);
        return k;
    }
}
