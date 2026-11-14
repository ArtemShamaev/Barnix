package java.nio;

public class ShortBuffer extends Buffer implements Comparable<ShortBuffer> {
    final short[] array;      /* non-null when backed by a short[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    ShortBuffer(short[] a, int base, int capacity) { super(capacity, a, base * 2); array = a; arrayBase = base; swap = false; }
    ShortBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static ShortBuffer allocate(int n) { return new ShortBuffer(new short[n], 0, n); }
    public static ShortBuffer wrap(short[] a) { return new ShortBuffer(a, 0, a.length); }
    public static ShortBuffer wrap(short[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        ShortBuffer b = new ShortBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private ShortBuffer derive(int start, int cap) {
        ShortBuffer b = array != null ? new ShortBuffer(array, arrayBase + start, cap) : new ShortBuffer((byte[]) hb, offset + start * 2, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public ShortBuffer slice() { return derive(position, limit - position); }
    public ShortBuffer duplicate() { ShortBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public ShortBuffer asReadOnlyBuffer() { ShortBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final short[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private short load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 2; return (short) Memory.get(hb, at, 2, swap); }
    private void store(int i, short v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 2; Memory.put(hb, at, 2, v, swap); }
    public short get() { return load(nextGet()); }
    public short get(int i) { return load(check(i)); }
    public ShortBuffer put(short v) { store(nextPut(), v); return this; }
    public ShortBuffer put(int i, short v) { writable(); store(check(i), v); return this; }
    public ShortBuffer get(short[] dst) { return get(dst, 0, dst.length); }
    public ShortBuffer get(short[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 2, dst, off * 2, len * 2, 2, swap);
        return this;
    }
    public final ShortBuffer put(short[] src) { return put(src, 0, src.length); }
    public ShortBuffer put(short[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 2, hb, offset + at * 2, len * 2, 2, swap);
        return this;
    }
    public ShortBuffer put(ShortBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public ShortBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof ShortBuffer && compareTo((ShortBuffer) o) == 0; }
    public int compareTo(ShortBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { short a = load(position + i), b = o.load(o.position + i); int c = Short.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
