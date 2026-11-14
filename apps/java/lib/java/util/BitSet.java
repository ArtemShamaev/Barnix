package java.util;

public class BitSet implements Cloneable, java.io.Serializable {
    private long[] words;
    public BitSet() { words = new long[1]; }
    public BitSet(int n) { words = new long[Math.max(1, (n + 63) >> 6)]; }
    private void ensure(int w) { if (w >= words.length) words = Arrays.copyOf(words, Math.max(words.length * 2, w + 1)); }
    public void set(int i) { ensure(i >> 6); words[i >> 6] |= 1L << i; }
    public void set(int i, boolean v) { if (v) set(i); else clear(i); }
    public void set(int from, int to) { for (int i = from; i < to; i++) set(i); }
    public void clear(int i) { if ((i >> 6) < words.length) words[i >> 6] &= ~(1L << i); }
    public void clear() { Arrays.fill(words, 0); }
    public boolean get(int i) { return (i >> 6) < words.length && (words[i >> 6] & 1L << i) != 0; }
    public void flip(int i) { set(i, !get(i)); }
    public int cardinality() { int n = 0; for (long w : words) n += Long.bitCount(w); return n; }
    public int nextSetBit(int i) { for (; (i >> 6) < words.length; i++) if (get(i)) return i; return -1; }
    public int nextClearBit(int i) { while (get(i)) i++; return i; }
    public int length() { for (int i = words.length * 64 - 1; i >= 0; i--) if (get(i)) return i + 1; return 0; }
    public int size() { return words.length * 64; }
    public boolean isEmpty() { for (long w : words) if (w != 0) return false; return true; }
    public void or(BitSet o) { ensure(o.words.length - 1); for (int i = 0; i < o.words.length; i++) words[i] |= o.words[i]; }
    public void and(BitSet o) { for (int i = 0; i < words.length; i++) words[i] &= i < o.words.length ? o.words[i] : 0; }
    public void andNot(BitSet o) { for (int i = 0; i < words.length && i < o.words.length; i++) words[i] &= ~o.words[i]; }
    public void xor(BitSet o) { ensure(o.words.length - 1); for (int i = 0; i < o.words.length; i++) words[i] ^= o.words[i]; }
    public Object clone() { BitSet b = new BitSet(); b.words = words.clone(); return b; }
    public boolean equals(Object o) {
        if (!(o instanceof BitSet)) return false;
        BitSet b = (BitSet) o;
        for (int i = 0; i < Math.max(words.length, b.words.length); i++) if ((i < words.length ? words[i] : 0) != (i < b.words.length ? b.words[i] : 0)) return false;
        return true;
    }
    public int hashCode() { long h = 1234; for (int i = words.length; --i >= 0;) h ^= words[i] * (i + 1); return (int) ((h >> 32) ^ h); }
    public String toString() {
        StringBuilder b = new StringBuilder("{");
        for (int i = nextSetBit(0); i >= 0; i = nextSetBit(i + 1)) { if (b.length() > 1) b.append(", "); b.append(i); }
        return b.append('}').toString();
    }
}
