package java.nio;

public class IntBuffer extends Buffer implements Comparable<IntBuffer> {
    final int[] array;      /* non-null when backed by a int[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    IntBuffer(int[] a, int base, int capacity) { super(capacity, a, base * 4); array = a; arrayBase = base; swap = false; }
    IntBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static IntBuffer allocate(int n) { return new IntBuffer(new int[n], 0, n); }
    public static IntBuffer wrap(int[] a) { return new IntBuffer(a, 0, a.length); }
    public static IntBuffer wrap(int[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        IntBuffer b = new IntBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private IntBuffer derive(int start, int cap) {
        IntBuffer b = array != null ? new IntBuffer(array, arrayBase + start, cap) : new IntBuffer((byte[]) hb, offset + start * 4, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public IntBuffer slice() { return derive(position, limit - position); }
    public IntBuffer duplicate() { IntBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public IntBuffer asReadOnlyBuffer() { IntBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final int[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private int load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 4; return Memory.get(hb, at, 4, swap); }
    private void store(int i, int v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 4; Memory.put(hb, at, 4, v, swap); }
    public int get() { return load(nextGet()); }
    public int get(int i) { return load(check(i)); }
    public IntBuffer put(int v) { store(nextPut(), v); return this; }
    public IntBuffer put(int i, int v) { writable(); store(check(i), v); return this; }
    public IntBuffer get(int[] dst) { return get(dst, 0, dst.length); }
    public IntBuffer get(int[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 4, dst, off * 4, len * 4, 4, swap);
        return this;
    }
    public final IntBuffer put(int[] src) { return put(src, 0, src.length); }
    public IntBuffer put(int[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 4, hb, offset + at * 4, len * 4, 4, swap);
        return this;
    }
    public IntBuffer put(IntBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public IntBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof IntBuffer && compareTo((IntBuffer) o) == 0; }
    public int compareTo(IntBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { int a = load(position + i), b = o.load(o.position + i); int c = Integer.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
