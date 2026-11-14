package java.nio;

public class DoubleBuffer extends Buffer implements Comparable<DoubleBuffer> {
    final double[] array;      /* non-null when backed by a double[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    DoubleBuffer(double[] a, int base, int capacity) { super(capacity, a, base * 8); array = a; arrayBase = base; swap = false; }
    DoubleBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static DoubleBuffer allocate(int n) { return new DoubleBuffer(new double[n], 0, n); }
    public static DoubleBuffer wrap(double[] a) { return new DoubleBuffer(a, 0, a.length); }
    public static DoubleBuffer wrap(double[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        DoubleBuffer b = new DoubleBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private DoubleBuffer derive(int start, int cap) {
        DoubleBuffer b = array != null ? new DoubleBuffer(array, arrayBase + start, cap) : new DoubleBuffer((byte[]) hb, offset + start * 8, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public DoubleBuffer slice() { return derive(position, limit - position); }
    public DoubleBuffer duplicate() { DoubleBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public DoubleBuffer asReadOnlyBuffer() { DoubleBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final double[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private double load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 8; return Double.longBitsToDouble(Memory.getLong(hb, at, swap)); }
    private void store(int i, double v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 8; Memory.putLong(hb, at, Double.doubleToRawLongBits(v), swap); }
    public double get() { return load(nextGet()); }
    public double get(int i) { return load(check(i)); }
    public DoubleBuffer put(double v) { store(nextPut(), v); return this; }
    public DoubleBuffer put(int i, double v) { writable(); store(check(i), v); return this; }
    public DoubleBuffer get(double[] dst) { return get(dst, 0, dst.length); }
    public DoubleBuffer get(double[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 8, dst, off * 8, len * 8, 8, swap);
        return this;
    }
    public final DoubleBuffer put(double[] src) { return put(src, 0, src.length); }
    public DoubleBuffer put(double[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 8, hb, offset + at * 8, len * 8, 8, swap);
        return this;
    }
    public DoubleBuffer put(DoubleBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public DoubleBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof DoubleBuffer && compareTo((DoubleBuffer) o) == 0; }
    public int compareTo(DoubleBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { double a = load(position + i), b = o.load(o.position + i); int c = Double.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
