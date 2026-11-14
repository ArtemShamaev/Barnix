package java.nio;

public class LongBuffer extends Buffer implements Comparable<LongBuffer> {
    final long[] array;      /* non-null when backed by a long[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    LongBuffer(long[] a, int base, int capacity) { super(capacity, a, base * 8); array = a; arrayBase = base; swap = false; }
    LongBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static LongBuffer allocate(int n) { return new LongBuffer(new long[n], 0, n); }
    public static LongBuffer wrap(long[] a) { return new LongBuffer(a, 0, a.length); }
    public static LongBuffer wrap(long[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        LongBuffer b = new LongBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private LongBuffer derive(int start, int cap) {
        LongBuffer b = array != null ? new LongBuffer(array, arrayBase + start, cap) : new LongBuffer((byte[]) hb, offset + start * 8, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public LongBuffer slice() { return derive(position, limit - position); }
    public LongBuffer duplicate() { LongBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public LongBuffer asReadOnlyBuffer() { LongBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final long[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private long load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 8; return Memory.getLong(hb, at, swap); }
    private void store(int i, long v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 8; Memory.putLong(hb, at, v, swap); }
    public long get() { return load(nextGet()); }
    public long get(int i) { return load(check(i)); }
    public LongBuffer put(long v) { store(nextPut(), v); return this; }
    public LongBuffer put(int i, long v) { writable(); store(check(i), v); return this; }
    public LongBuffer get(long[] dst) { return get(dst, 0, dst.length); }
    public LongBuffer get(long[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 8, dst, off * 8, len * 8, 8, swap);
        return this;
    }
    public final LongBuffer put(long[] src) { return put(src, 0, src.length); }
    public LongBuffer put(long[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 8, hb, offset + at * 8, len * 8, 8, swap);
        return this;
    }
    public LongBuffer put(LongBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public LongBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof LongBuffer && compareTo((LongBuffer) o) == 0; }
    public int compareTo(LongBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { long a = load(position + i), b = o.load(o.position + i); int c = Long.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
