package java.nio;

public class CharBuffer extends Buffer implements Comparable<CharBuffer>, CharSequence {
    final char[] array;      /* non-null when backed by a char[] */
    final int arrayBase;
    final boolean swap;     /* view over big-endian bytes */

    CharBuffer(char[] a, int base, int capacity) { super(capacity, a, base * 2); array = a; arrayBase = base; swap = false; }
    CharBuffer(byte[] b, int byteOffset, int capacity, boolean bigEndian) { super(capacity, b, byteOffset); array = null; arrayBase = 0; swap = bigEndian; }
    public static CharBuffer allocate(int n) { return new CharBuffer(new char[n], 0, n); }
    public static CharBuffer wrap(char[] a) { return new CharBuffer(a, 0, a.length); }
    public static CharBuffer wrap(char[] a, int off, int len) {
        if (off < 0 || len < 0 || off > a.length - len) throw new IndexOutOfBoundsException();
        CharBuffer b = new CharBuffer(a, 0, a.length);
        b.position = off; b.limit = off + len;
        return b;
    }
    private CharBuffer derive(int start, int cap) {
        CharBuffer b = array != null ? new CharBuffer(array, arrayBase + start, cap) : new CharBuffer((byte[]) hb, offset + start * 2, cap, swap);
        b.direct = direct; b.readOnly = readOnly;
        return b;
    }
    public CharBuffer slice() { return derive(position, limit - position); }
    public CharBuffer duplicate() { CharBuffer b = derive(0, capacity); b.position = position; b.limit = limit; b.mark = mark; return b; }
    public CharBuffer asReadOnlyBuffer() { CharBuffer b = duplicate(); b.readOnly = true; return b; }
    public final boolean hasArray() { return array != null && !readOnly; }
    public final char[] array() { if (array == null) throw new UnsupportedOperationException(); return array; }
    public final int arrayOffset() { return arrayBase; }
    public ByteOrder order() { return swap ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN; }

    private char load(int i) { if (array != null) return array[arrayBase + i]; int at = offset + i * 2; return (char) Memory.get(hb, at, 2, swap); }
    private void store(int i, char v) { if (array != null) { array[arrayBase + i] = v; return; } int at = offset + i * 2; Memory.put(hb, at, 2, v, swap); }
    public char get() { return load(nextGet()); }
    public char get(int i) { return load(check(i)); }
    public CharBuffer put(char v) { store(nextPut(), v); return this; }
    public CharBuffer put(int i, char v) { writable(); store(check(i), v); return this; }
    public CharBuffer get(char[] dst) { return get(dst, 0, dst.length); }
    public CharBuffer get(char[] dst, int off, int len) {
        if (off < 0 || len < 0 || off > dst.length - len) throw new IndexOutOfBoundsException();
        int at = nextGet(len);
        if (array != null) System.arraycopy(array, arrayBase + at, dst, off, len);
        else Memory.copy(hb, offset + at * 2, dst, off * 2, len * 2, 2, swap);
        return this;
    }
    public final CharBuffer put(char[] src) { return put(src, 0, src.length); }
    public CharBuffer put(char[] src, int off, int len) {
        if (off < 0 || len < 0 || off > src.length - len) throw new IndexOutOfBoundsException();
        int at = nextPut(len);
        if (array != null) System.arraycopy(src, off, array, arrayBase + at, len);
        else Memory.copy(src, off * 2, hb, offset + at * 2, len * 2, 2, swap);
        return this;
    }
    public CharBuffer put(CharBuffer src) {
        if (src == this) throw new IllegalArgumentException();
        int n = src.remaining();
        if (array != null && src.array != null) { System.arraycopy(src.array, src.arrayBase + src.nextGet(n), array, arrayBase + nextPut(n), n); return this; }
        for (int i = 0; i < n; i++) put(src.get());
        return this;
    }
    public CharBuffer compact() {
        writable();
        int n = remaining();
        for (int i = 0; i < n; i++) store(i, load(position + i));
        position = n; limit = capacity; mark = -1;
        return this;
    }
    public int hashCode() { int h = 1; for (int i = limit - 1; i >= position; i--) h = 31 * h + (int) load(i); return h; }
    public boolean equals(Object o) { return o instanceof CharBuffer && compareTo((CharBuffer) o) == 0; }
    public int compareTo(CharBuffer o) {
        int n = Math.min(remaining(), o.remaining());
        for (int i = 0; i < n; i++) { char a = load(position + i), b = o.load(o.position + i); int c = Character.compare(a, b); if (c != 0) return c; }
        return remaining() - o.remaining();
    }
    public int length() { return remaining(); }
    public char charAt(int i) { return get(position + i); }
    public CharSequence subSequence(int s, int e) { StringBuilder b = new StringBuilder(); for (int i = s; i < e; i++) b.append(charAt(i)); return b.toString(); }
    public static CharBuffer wrap(CharSequence s) { return wrap(s.toString().toCharArray()); }
    public String toString() { StringBuilder b = new StringBuilder(); for (int i = position; i < limit; i++) b.append(load(i)); return b.toString(); }
    String describe() { return getClass().getName() + "[pos=" + position + " lim=" + limit + " cap=" + capacity + "]"; }
}
