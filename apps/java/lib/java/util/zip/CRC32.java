package java.util.zip;

public class CRC32 implements Checksum {
    private int crc;
    private static native int update0(int crc, byte[] b, int off, int len);
    public CRC32() {}
    public void update(int b) { crc = update0(crc, new byte[] {(byte) b}, 0, 1); }
    public void update(byte[] b, int off, int len) { crc = update0(crc, b, off, len); }
    public void update(byte[] b) { update(b, 0, b.length); }
    public long getValue() { return crc & 0xffffffffL; }
    public void reset() { crc = 0; }
}
