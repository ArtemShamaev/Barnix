package java.util.zip;

import java.io.*;
import java.util.*;

/* Reads a whole archive into memory and indexes its central directory. */
public class ZipFile implements Closeable {
    public static final int OPEN_READ = 1, OPEN_DELETE = 4;
    private final String name;
    private byte[] data;
    private final LinkedHashMap<String, ZipEntry> entries = new LinkedHashMap<String, ZipEntry>();

    public ZipFile(String name) throws IOException { this(new File(name)); }
    public ZipFile(File file, int mode) throws IOException { this(file); }
    public ZipFile(File file) throws IOException {
        name = file.getPath();
        FileInputStream in = new FileInputStream(file);
        try { data = in.readAllBytes(); } finally { in.close(); }
        int end = data.length - 22;
        while (end >= 0 && !(u32(end) == 0x06054b50L)) end--;
        if (end < 0) throw new ZipException("zip END header not found");
        int count = u16(end + 10);
        int at = (int) u32(end + 16);
        for (int i = 0; i < count; i++) {
            if (u32(at) != 0x02014b50L) throw new ZipException("invalid CEN header");
            int nl = u16(at + 28), el = u16(at + 30), cl = u16(at + 32);
            ZipEntry e = new ZipEntry(new String(data, at + 46, nl, "UTF-8"));
            e.method = u16(at + 10);
            e.crc = u32(at + 16);
            e.csize = u32(at + 20);
            e.size = u32(at + 24);
            e.offset = (int) u32(at + 42);
            entries.put(e.name, e);
            at += 46 + nl + el + cl;
        }
    }
    private int u16(int p) { return (data[p] & 0xff) | (data[p + 1] & 0xff) << 8; }
    private long u32(int p) { return (u16(p) | (long) u16(p + 2) << 16) & 0xffffffffL; }
    public ZipEntry getEntry(String n) { ZipEntry e = entries.get(n); if (e == null) e = entries.get(n + "/"); return e; }
    public InputStream getInputStream(ZipEntry e) throws IOException {
        ZipEntry z = entries.get(e.getName());
        if (z == null) return null;
        int p = z.offset;
        if (u32(p) != 0x04034b50L) throw new ZipException("invalid LOC header");
        int start = p + 30 + u16(p + 26) + u16(p + 28);
        if (z.method == 0) return new ByteArrayInputStream(data, start, (int) z.size);
        Inflater inf = new Inflater(true);
        inf.setInput(data, start, (int) z.csize);
        return new InflaterInputStream(new ByteArrayInputStream(new byte[0]), inf) { protected void load() {} };
    }
    public Enumeration<? extends ZipEntry> entries() { return Collections.enumeration(new ArrayList<ZipEntry>(entries.values())); }
    public int size() { return entries.size(); }
    public String getName() { return name; }
    public void close() { data = null; }
}
