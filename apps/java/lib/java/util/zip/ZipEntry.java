package java.util.zip;

public class ZipEntry implements Cloneable {
    public static final int STORED = 0, DEFLATED = 8;
    String name, comment;
    long size = -1, csize = -1, crc = -1, time = -1;
    int method = -1, offset;
    byte[] extra;
    public ZipEntry(String name) { if (name == null) throw new NullPointerException("name"); this.name = name; }
    public ZipEntry(ZipEntry e) { name = e.name; size = e.size; csize = e.csize; crc = e.crc; method = e.method; time = e.time; offset = e.offset; }
    public String getName() { return name; }
    public long getSize() { return size; }
    public void setSize(long s) { size = s; }
    public long getCompressedSize() { return csize; }
    public void setCompressedSize(long s) { csize = s; }
    public long getCrc() { return crc; }
    public void setCrc(long c) { crc = c; }
    public int getMethod() { return method; }
    public void setMethod(int m) { method = m; }
    public long getTime() { return time; }
    public void setTime(long t) { time = t; }
    public byte[] getExtra() { return extra; }
    public void setExtra(byte[] e) { extra = e; }
    public String getComment() { return comment; }
    public void setComment(String c) { comment = c; }
    public boolean isDirectory() { return name.endsWith("/"); }
    public String toString() { return name; }
    public int hashCode() { return name.hashCode(); }
    public Object clone() { return new ZipEntry(this); }
}
