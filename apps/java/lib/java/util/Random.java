package java.util;

public class Random implements java.io.Serializable {
    private long seed;
    private double nextNextGaussian;
    private boolean haveNextNextGaussian;
    private static long seedUniquifier = 8682522807148012L;

    public Random() { this((seedUniquifier *= 181783497276652981L) ^ System.nanoTime()); }
    public Random(long seed) { setSeed(seed); }
    public synchronized void setSeed(long s) { seed = (s ^ 0x5DEECE66DL) & ((1L << 48) - 1); haveNextNextGaussian = false; }
    protected int next(int bits) {
        seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
        return (int) (seed >>> (48 - bits));
    }
    public void nextBytes(byte[] bytes) {
        for (int i = 0, len = bytes.length; i < len;)
            for (int rnd = nextInt(), n = Math.min(len - i, 4); n-- > 0; rnd >>= 8) bytes[i++] = (byte) rnd;
    }
    public int nextInt() { return next(32); }
    public int nextInt(int bound) {
        if (bound <= 0) throw new IllegalArgumentException("bound must be positive");
        if ((bound & -bound) == bound) return (int) ((bound * (long) next(31)) >> 31);
        int bits, val;
        do { bits = next(31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
        return val;
    }
    public int nextInt(int origin, int bound) { return origin + nextInt(bound - origin); }
    public long nextLong() { return ((long) next(32) << 32) + next(32); }
    public boolean nextBoolean() { return next(1) != 0; }
    public float nextFloat() { return next(24) / ((float) (1 << 24)); }
    public double nextDouble() { return (((long) next(26) << 27) + next(27)) * 0x1.0p-53; }
    public synchronized double nextGaussian() {
        if (haveNextNextGaussian) { haveNextNextGaussian = false; return nextNextGaussian; }
        double v1, v2, s;
        do { v1 = 2 * nextDouble() - 1; v2 = 2 * nextDouble() - 1; s = v1 * v1 + v2 * v2; } while (s >= 1 || s == 0);
        double m = StrictMath.sqrt(-2 * StrictMath.log(s) / s);
        nextNextGaussian = v2 * m;
        haveNextNextGaussian = true;
        return v1 * m;
    }
}
