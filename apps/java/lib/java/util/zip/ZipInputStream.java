package java.util.zip;

import java.io.*;

public class ZipInputStream extends InflaterInputStream {
    private final PushbackInputStream src;
    private ZipEntry entry;
    private InputStream current;
    public ZipInputStream(InputStream in) { super(new PushbackInputStream(in, 65536), new Inflater(true)); src = (PushbackInputStream) this.in; }
    private static int u16(byte[] b, int p) { return (b[p] & 0xff) | (b[p + 1] & 0xff) << 8; }
    private static long u32(byte[] b, int p) { return (u16(b, p) | (long) u16(b, p + 2) << 16) & 0xffffffffL; }
    public ZipEntry getNextEntry() throws IOException {
        if (current != null) closeEntry();
        DataInputStream d = new DataInputStream(src);
        byte[] h = new byte[30];
        try { d.readFully(h); } catch (EOFException e) { return null; }
        if (u32(h, 0) != 0x04034b50L) return null;
        int flags = u16(h, 6);
        byte[] nb = new byte[u16(h, 26)];
        d.readFully(nb);
        d.skipBytes(u16(h, 28));
        entry = new ZipEntry(new String(nb, "UTF-8"));
        entry.method = u16(h, 8);
        entry.csize = u32(h, 18);
        entry.size = u32(h, 22);
        if ((flags & 8) != 0) throw new ZipException("streamed entries with data descriptors are not supported");
        byte[] body = new byte[(int) entry.csize];
        d.readFully(body);
        if (entry.method == 0) current = new ByteArrayInputStream(body);
        else { Inflater inf = new Inflater(true); inf.setInput(body); current = new InflaterInputStream(new ByteArrayInputStream(new byte[0]), inf) { protected void load() {} }; }
        return entry;
    }
    public void closeEntry() throws IOException { current = null; }
    public int read(byte[] b, int off, int len) throws IOException { return current == null ? -1 : current.read(b, off, len); }
    public int available() throws IOException { return current == null ? 0 : 1; }
    public void close() throws IOException { src.close(); }
}
