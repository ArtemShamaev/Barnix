package java.nio;

public class FloatBuffer extends Buffer implements Comparable<FloatBuffer> {
    final float[] array;      /* non-null when backed by a float[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    FloatBuffer(float[] a, int base, int capacity) { super(capacity, a, base * 4); array = a; arrayBase = base; swap = false; }
    FloatBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static FloatBuffer allocate(int n) { return new FloatBuffer(new float[n], 0, n); }
    public static FloatBuffer wrap(float[] a) { return new FloatBuffer(a, 0, a.length); }
    public static FloatBuffer wrap(float[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        FloatBuffer b = new FloatBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private FloatBuffer derive(int start, int cap) {
        FloatBuffer b = array != null ? new FloatBuffer(array, arrayBase + start, cap) : new FloatBuffer((byte[]) hb, offset + start * 4, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public FloatBuffer slice() { return derive(position, limit - position); }
    public FloatBuffer duplicate() { FloatBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public FloatBuffer asReadOnlyBuffer() { FloatBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final float[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private float load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 4; return Float.intBitsToFloat(Memory.get(hb, at, 4, swap)); }
    private void store(int i, float v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 4; Memory.put(hb, at, 4, Float.floatToRawIntBits(v), swap); }
    public float get() { return load(nextGet()); }
    public float get(int i) { return load(check(i)); }
    public FloatBuffer put(float v) { store(nextPut(), v); return this; }
    public FloatBuffer put(int i, float v) { writable(); store(check(i), v); return this; }
    public FloatBuffer get(float[] dst) { return get(dst, 0, dst.length); }
    public FloatBuffer get(float[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 4, dst, off * 4, len * 4, 4, swap);
        return this;
    }
    public final FloatBuffer put(float[] src) { return put(src, 0, src.length); }
    public FloatBuffer put(float[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 4, hb, offset + at * 4, len * 4, 4, swap);
        return this;
    }
    public FloatBuffer put(FloatBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public FloatBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof FloatBuffer && compareTo((FloatBuffer) o) == 0; }
    public int compareTo(FloatBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { float a = load(position + i), b = o.load(o.position + i); int c = Float.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public String toString() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
