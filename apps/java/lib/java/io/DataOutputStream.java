package java.io;

public class DataOutputStream extends FilterOutputStream implements DataOutput {
    protected int written;
    private final byte[] scratch = new byte[8];
    public DataOutputStream(OutputStream out) { super(out); }
    public synchronized void write(int b) throws IOException { out.write(b); written++; }
    public synchronized void write(byte[] b, int off, int len) throws IOException { out.write(b, off, len); written += len; }
    public void flush() throws IOException { out.flush(); }
    public final void writeBoolean(boolean v) throws IOException { write(v ? 1 : 0); }
    public final void writeByte(int v) throws IOException { write(v); }
    public final void writeShort(int v) throws IOException { scratch[0] = (byte) (v >> 8); scratch[1] = (byte) v; write(scratch, 0, 2); }
    public final void writeChar(int v) throws IOException { writeShort(v); }
    public final void writeInt(int v) throws IOException {
        scratch[0] = (byte) (v >> 24); scratch[1] = (byte) (v >> 16); scratch[2] = (byte) (v >> 8); scratch[3] = (byte) v;
        write(scratch, 0, 4);
    }
    public final void writeLong(long v) throws IOException {
        for (int i = 0; i < 8; i++) scratch[i] = (byte) (v >> (56 - 8 * i));
        write(scratch, 0, 8);
    }
    public final void writeFloat(float v) throws IOException { writeInt(Float.floatToIntBits(v)); }
    public final void writeDouble(double v) throws IOException { writeLong(Double.doubleToLongBits(v)); }
    public final void writeBytes(String s) throws IOException { for (int i = 0; i < s.length(); i++) write((byte) s.charAt(i)); }
    public final void writeChars(String s) throws IOException { for (int i = 0; i < s.length(); i++) writeChar(s.charAt(i)); }
    public final void writeUTF(String s) throws IOException { writeUTF(s, this); }
    static int writeUTF(String s, DataOutput out) throws IOException {
        int n = 0;
        for (int i = 0; i < s.length(); i++) { char c = s.charAt(i); n += c >= 1 && c < 0x80 ? 1 : c < 0x800 ? 2 : 3; }
        if (n > 65535) throw new UTFDataFormatException("encoded string too long: " + n + " bytes");
        byte[] b = new byte[n + 2];
        b[0] = (byte) (n >> 8); b[1] = (byte) n;
        int k = 2;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c >= 1 && c < 0x80) b[k++] = (byte) c;
            else if (c < 0x800) { b[k++] = (byte) (0xc0 | c >> 6); b[k++] = (byte) (0x80 | (c & 0x3f)); }
            else { b[k++] = (byte) (0xe0 | c >> 12); b[k++] = (byte) (0x80 | (c >> 6 & 0x3f)); b[k++] = (byte) (0x80 | (c & 0x3f)); }
        }
        out.write(b, 0, k);
        return k;
    }
    public final int size() { return written; }
}
