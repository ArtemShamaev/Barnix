package java.nio;

public class ByteBuffer extends Buffer implements Comparable<ByteBuffer> {
    final byte[] bytes;
    final int base;     /* index of element 0 in bytes */
    boolean bigEndian = true;

    ByteBuffer(byte[] b, int base, int capacity) { super(capacity, b, base); bytes = b; this.base = base; }
    public static ByteBuffer allocate(int n) { return new ByteBuffer(new byte[n], 0, n); }
    public static ByteBuffer allocateDirect(int n) { ByteBuffer b = new ByteBuffer(new byte[n], 0, n); b.direct = true; return b; }
    public static ByteBuffer wrap(byte[] a) { return wrap(a, 0, a.length); }
    public static ByteBuffer wrap(byte[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        ByteBuffer b = new ByteBuffer(a, 0, a.length);
        b.position = off;
        b.limit = off + len;
        return b;
    }
    private ByteBuffer derive(int start, int cap) {
        ByteBuffer b = new ByteBuffer(bytes, base + start, cap);
        b.direct = direct; b.readOnly = readOnly; b.bigEndian = bigEndian;
        return b;
    }
    public ByteBuffer slice() { return derive(position, limit - position); }
    public ByteBuffer duplicate() { ByteBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public ByteBuffer asReadOnlyBuffer() { ByteBuffer b = duplicate(); b.readOnly = true; return b; }
    public final ByteOrder order() { return bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }
    public final ByteBuffer order(ByteOrder o) { bigEndian = o == ByteOrder.BIG_ENDIAN; return this; }
    public final boolean hasArray() { return !direct && !readOnly; }
    public final byte[] array() { if (direct) throw new UnsupportedOperationException(); if (readOnly) throw new ReadOnlyBufferException(); return bytes; }
    public final int arrayOffset() { return base; }

    public byte get() { return bytes[base + nextGet()]; }
    public byte get(int i) { return bytes[base + check(i)]; }
    public ByteBuffer put(byte b) { bytes[base + nextPut()] = b; return this; }
    public ByteBuffer put(int i, byte b) { writable(); bytes[base + check(i)] = b; return this; }
    public ByteBuffer get(byte[] dst) { return get(dst, 0, dst.length); }
    public ByteBuffer get(byte[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        System.arraycopy(bytes, base + nextGet(len), dst, off, len);
        return this;
    }
    public final ByteBuffer put(byte[] src) { return put(src, 0, src.length); }
    public ByteBuffer put(byte[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        System.arraycopy(src, off, bytes, base + nextPut(len), len);
        return this;
    }
    public ByteBuffer put(ByteBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        System.arraycopy(src.bytes, src.base + src.nextGet(n), bytes, base + nextPut(n), n);
        return this;
    }
    public ByteBuffer compact() {
        writable();
        int n = remaining();
        System.arraycopy(bytes, base + position, bytes, base, n);
        position = n; limit = capacity; mark = -1;
        return this;
    }
    private boolean swap() { return bigEndian; }
    public char getChar() { return (char) Memory.get(bytes, base + nextGet(2), 2, swap()); }
    public char getChar(int i) { return (char) Memory.get(bytes, base + check(i, 2), 2, swap()); }
    public ByteBuffer putChar(char v) { Memory.put(bytes, base + nextPut(2), 2, v, swap()); return this; }
    public ByteBuffer putChar(int i, char v) { writable(); Memory.put(bytes, base + check(i, 2), 2, v, swap()); return this; }
    public short getShort() { return (short) Memory.get(bytes, base + nextGet(2), 2, swap()); }
    public short getShort(int i) { return (short) Memory.get(bytes, base + check(i, 2), 2, swap()); }
    public ByteBuffer putShort(short v) { Memory.put(bytes, base + nextPut(2), 2, v, swap()); return this; }
    public ByteBuffer putShort(int i, short v) { writable(); Memory.put(bytes, base + check(i, 2), 2, v, swap()); return this; }
    public int getInt() { return Memory.get(bytes, base + nextGet(4), 4, swap()); }
    public int getInt(int i) { return Memory.get(bytes, base + check(i, 4), 4, swap()); }
    public ByteBuffer putInt(int v) { Memory.put(bytes, base + nextPut(4), 4, v, swap()); return this; }
    public ByteBuffer putInt(int i, int v) { writable(); Memory.put(bytes, base + check(i, 4), 4, v, swap()); return this; }
    public long getLong() { return Memory.getLong(bytes, base + nextGet(8), swap()); }
    public long getLong(int i) { return Memory.getLong(bytes, base + check(i, 8), swap()); }
    public ByteBuffer putLong(long v) { Memory.putLong(bytes, base + nextPut(8), v, swap()); return this; }
    public ByteBuffer putLong(int i, long v) { writable(); Memory.putLong(bytes, base + check(i, 8), v, swap()); return this; }
    public float getFloat() { return Float.intBitsToFloat(getInt()); }
    public float getFloat(int i) { return Float.intBitsToFloat(getInt(i)); }
    public ByteBuffer putFloat(float v) { return putInt(Float.floatToRawIntBits(v)); }
    public ByteBuffer putFloat(int i, float v) { return putInt(i, Float.floatToRawIntBits(v)); }
    public double getDouble() { return Double.longBitsToDouble(getLong()); }
    public double getDouble(int i) { return Double.longBitsToDouble(getLong(i)); }
    public ByteBuffer putDouble(double v) { return putLong(Double.doubleToRawLongBits(v)); }
    public ByteBuffer putDouble(int i, double v) { return putLong(i, Double.doubleToRawLongBits(v)); }

    private <T extends Buffer> T view(T b) { b.direct = direct; b.readOnly = readOnly; return b; }
    public IntBuffer asIntBuffer() { return view(new IntBuffer(bytes, base + position, remaining() >> 2, bigEndian)); }
    public FloatBuffer asFloatBuffer() { return view(new FloatBuffer(bytes, base + position, remaining() >> 2, bigEndian)); }
    public ShortBuffer asShortBuffer() { return view(new ShortBuffer(bytes, base + position, remaining() >> 1, bigEndian)); }
    public CharBuffer asCharBuffer() { return view(new CharBuffer(bytes, base + position, remaining() >> 1, bigEndian)); }
    public LongBuffer asLongBuffer() { return view(new LongBuffer(bytes, base + position, remaining() >> 3, bigEndian)); }
    public DoubleBuffer asDoubleBuffer() { return view(new DoubleBuffer(bytes, base + position, remaining() >> 3, bigEndian)); }

    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + get(i); return h; }
    public boolean equals(Object o) { return o instanceof ByteBuffer && compareTo((ByteBuffer) o) == 0; }
    public int compareTo(ByteBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { int c = Byte.compare(get(position + i), o.get(o.position + i)); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
