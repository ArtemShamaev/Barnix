package java.io;

public class DataInputStream extends FilterInputStream implements DataInput {
    private final byte[] scratch = new byte[8];
    public DataInputStream(InputStream in) { super(in); }
    public final int read(byte[] b) throws IOException { return in.read(b, 0, b.length); }
    public final int read(byte[] b, int off, int len) throws IOException { return in.read(b, off, len); }
    public final void readFully(byte[] b) throws IOException { readFully(b, 0, b.length); }
    public final void readFully(byte[] b, int off, int len) throws IOException {
        if (len < 0) throw new IndexOutOfBoundsException();
        int n = 0;
        while (n < len) { int k = in.read(b, off + n, len - n); if (k < 0) throw new EOFException(); n += k; }
    }
    public final int skipBytes(int n) throws IOException { int total = 0, cur; while (total < n && (cur = (int) in.skip(n - total)) > 0) total += cur; return total; }
    public final boolean readBoolean() throws IOException { return readUnsignedByte() != 0; }
    public final byte readByte() throws IOException { return (byte) readUnsignedByte(); }
    public final int readUnsignedByte() throws IOException { int c = in.read(); if (c < 0) throw new EOFException(); return c; }
    public final short readShort() throws IOException { readFully(scratch, 0, 2); return (short) ((scratch[0] & 0xff) << 8 | (scratch[1] & 0xff)); }
    public final int readUnsignedShort() throws IOException { readFully(scratch, 0, 2); return (scratch[0] & 0xff) << 8 | (scratch[1] & 0xff); }
    public final char readChar() throws IOException { return (char) readUnsignedShort(); }
    public final int readInt() throws IOException {
        readFully(scratch, 0, 4);
        return (scratch[0] & 0xff) << 24 | (scratch[1] & 0xff) << 16 | (scratch[2] & 0xff) << 8 | (scratch[3] & 0xff);
    }
    public final long readLong() throws IOException {
        readFully(scratch, 0, 8);
        long v = 0;
        for (int i = 0; i < 8; i++) v = v << 8 | (scratch[i] & 0xff);
        return v;
    }
    public final float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
    public final double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }
    @Deprecated
    public final String readLine() throws IOException {
        StringBuilder b = new StringBuilder();
        int c;
        while ((c = in.read()) != -1 && c != '\n') if (c != '\r') b.append((char) c);
        return c == -1 && b.length() == 0 ? null : b.toString();
    }
    public final String readUTF() throws IOException { return readUTF(this); }
    public static final String readUTF(DataInput in) throws IOException {
        int n = in.readUnsignedShort();
        byte[] b = new byte[n];
        in.readFully(b, 0, n);
        char[] c = new char[n];
        int k = 0;
        for (int i = 0; i < n;) {
            int x = b[i] & 0xff;
            if (x < 0x80) { c[k++] = (char) x; i++; }
            else if ((x & 0xe0) == 0xc0) {
                if (i + 1 >= n) throw new UTFDataFormatException("malformed input: partial character at end");
                c[k++] = (char) ((x & 0x1f) << 6 | (b[i + 1] & 0x3f)); i += 2;
            } else if ((x & 0xf0) == 0xe0) {
                if (i + 2 >= n) throw new UTFDataFormatException("malformed input: partial character at end");
                c[k++] = (char) ((x & 0x0f) << 12 | (b[i + 1] & 0x3f) << 6 | (b[i + 2] & 0x3f)); i += 3;
            } else throw new UTFDataFormatException("malformed input around byte " + i);
        }
        return new String(c, 0, k);
    }
}
