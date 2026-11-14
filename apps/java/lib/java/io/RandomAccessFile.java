package java.io;

/* Whole-file buffer: reads come from memory, writes go back on close/flush-like seek. */
public class RandomAccessFile implements DataOutput, DataInput, Closeable {
    private final String path;
    private final boolean writable;
    private byte[] data;
    private int length;
    private long pointer;
    private boolean dirty;

    public RandomAccessFile(String name, String mode) throws FileNotFoundException { this(new File(name), mode); }
    public RandomAccessFile(File f, String mode) throws FileNotFoundException {
        path = f.getPath();
        writable = mode.indexOf('w') >= 0;
        if (!f.exists()) {
            if (!writable) throw new FileNotFoundException(path + " (No such file or directory)");
            data = new byte[1024];
            dirty = true;
            return;
        }
        int n = (int) f.length();
        data = new byte[Math.max(n, 1024)];
        try { int at = 0; while (at < n) { int k = FileInputStream.read0(path, at, data, at, n - at); if (k <= 0) break; at += k; } length = at; }
        catch (Throwable e) { throw new FileNotFoundException(path); }
    }
    public long getFilePointer() { return pointer; }
    public void seek(long p) throws IOException { if (p < 0) throw new IOException("Negative seek offset"); pointer = p; }
    public long length() { return length; }
    public void setLength(long n) { ensure((int) n); length = (int) n; if (pointer > n) pointer = n; dirty = true; }
    private void ensure(int n) { if (n > data.length) data = java.util.Arrays.copyOf(data, Math.max(n, data.length * 2)); }
    public int read() { return pointer < length ? data[(int) pointer++] & 0xff : -1; }
    public int read(byte[] b) { return read(b, 0, b.length); }
    public int read(byte[] b, int off, int len) {
        if (pointer >= length) return -1;
        int n = (int) Math.min(len, length - pointer);
        System.arraycopy(data, (int) pointer, b, off, n);
        pointer += n;
        return n;
    }
    public void readFully(byte[] b) throws IOException { readFully(b, 0, b.length); }
    public void readFully(byte[] b, int off, int len) throws IOException { if (read(b, off, len) < len && len > 0) throw new EOFException(); }
    public int skipBytes(int n) { int k = (int) Math.max(0, Math.min(n, length - pointer)); pointer += k; return k; }
    public void write(int b) throws IOException {
        if (!writable) throw new IOException("Read-only file");
        ensure((int) pointer + 1);
        data[(int) pointer++] = (byte) b;
        if (pointer > length) length = (int) pointer;
        dirty = true;
    }
    public void write(byte[] b) throws IOException { write(b, 0, b.length); }
    public void write(byte[] b, int off, int len) throws IOException {
        if (!writable) throw new IOException("Read-only file");
        ensure((int) pointer + len);
        System.arraycopy(b, off, data, (int) pointer, len);
        pointer += len;
        if (pointer > length) length = (int) pointer;
        dirty = true;
    }
    public void close() throws IOException {
        if (dirty && writable && !FileOutputStream.write0(path, data, 0, length, false)) throw new IOException("Write error: " + path);
        dirty = false;
    }
    private final byte[] s = new byte[8];
    public boolean readBoolean() throws IOException { return readUnsignedByte() != 0; }
    public byte readByte() throws IOException { return (byte) readUnsignedByte(); }
    public int readUnsignedByte() throws IOException { int c = read(); if (c < 0) throw new EOFException(); return c; }
    public short readShort() throws IOException { return (short) readUnsignedShort(); }
    public int readUnsignedShort() throws IOException { readFully(s, 0, 2); return (s[0] & 0xff) << 8 | (s[1] & 0xff); }
    public char readChar() throws IOException { return (char) readUnsignedShort(); }
    public int readInt() throws IOException { readFully(s, 0, 4); return (s[0] & 0xff) << 24 | (s[1] & 0xff) << 16 | (s[2] & 0xff) << 8 | (s[3] & 0xff); }
    public long readLong() throws IOException { return ((long) readInt() << 32) | (readInt() & 0xffffffffL); }
    public float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
    public double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }
    public String readLine() throws IOException {
        StringBuilder b = new StringBuilder();
        int c;
        while ((c = read()) != -1 && c != '\n') if (c != '\r') b.append((char) c);
        return c == -1 && b.length() == 0 ? null : b.toString();
    }
    public String readUTF() throws IOException { return DataInputStream.readUTF(this); }
    public void writeBoolean(boolean v) throws IOException { write(v ? 1 : 0); }
    public void writeByte(int v) throws IOException { write(v); }
    public void writeShort(int v) throws IOException { write(v >> 8); write(v); }
    public void writeChar(int v) throws IOException { writeShort(v); }
    public void writeInt(int v) throws IOException { write(v >> 24); write(v >> 16); write(v >> 8); write(v); }
    public void writeLong(long v) throws IOException { writeInt((int) (v >> 32)); writeInt((int) v); }
    public void writeFloat(float v) throws IOException { writeInt(Float.floatToIntBits(v)); }
    public void writeDouble(double v) throws IOException { writeLong(Double.doubleToLongBits(v)); }
    public void writeBytes(String str) throws IOException { for (int i = 0; i < str.length(); i++) write(str.charAt(i)); }
    public void writeChars(String str) throws IOException { for (int i = 0; i < str.length(); i++) writeChar(str.charAt(i)); }
    public void writeUTF(String str) throws IOException { DataOutputStream.writeUTF(str, this); }
}
