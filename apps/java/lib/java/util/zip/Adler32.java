package java.util.zip;

public class Adler32 implements Checksum {
    private int a = 1, b;
    public void update(int x) { a = (a + (x & 0xff)) % 65521; b = (b + a) % 65521; }
    public void update(byte[] d, int off, int len) { for (int i = 0; i < len; i++) { a = (a + (d[off + i] & 0xff)) % 65521; b = (b + a) % 65521; } }
    public void update(byte[] d) { update(d, 0, d.length); }
    public long getValue() { return ((long) b << 16 | a) & 0xffffffffL; }
    public void reset() { a = 1; b = 0; }
}
