package java.lang;

public final class Long extends Number implements Comparable<Long> {
    public static final long MIN_VALUE = 0x8000000000000000L, MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64, BYTES = 8;
    @SuppressWarnings("unchecked")
    public static final Class<Long> TYPE = (Class<Long>) Class.getPrimitiveClass("long");
    private final long value;

    public Long(long v) { value = v; }
    public Long(String s) throws NumberFormatException { value = parseLong(s, 10); }
    public static Long valueOf(long v) { return new Long(v); }
    public static Long valueOf(String s) throws NumberFormatException { return new Long(parseLong(s, 10)); }
    public static Long valueOf(String s, int radix) throws NumberFormatException { return new Long(parseLong(s, radix)); }
    public static long parseLong(String s) throws NumberFormatException { return parseLong(s, 10); }
    public static long parseLong(String s, int radix) throws NumberFormatException { return parse(s, radix, MIN_VALUE, MAX_VALUE); }
    public static Long decode(String s) {
        boolean neg = s.startsWith("-");
        String b = neg || s.startsWith("+") ? s.substring(1) : s;
        int radix = 10;
        if (b.startsWith("0x") || b.startsWith("0X")) { radix = 16; b = b.substring(2); }
        else if (b.startsWith("#")) { radix = 16; b = b.substring(1); }
        else if (b.startsWith("0") && b.length() > 1) { radix = 8; b = b.substring(1); }
        return valueOf(parseLong(neg ? "-" + b : b, radix));
    }

    static long parse(String s, int radix, long min, long max) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("null");
        int n = s.length(), i = 0;
        if (n == 0) throw new NumberFormatException("For input string: \"\"");
        boolean neg = false;
        char first = s.charAt(0);
        if (first == '-' || first == '+') { neg = first == '-'; i = 1; if (n == 1) throw new NumberFormatException("For input string: \"" + s + "\""); }
        long result = 0;
        boolean overflow = false;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException("For input string: \"" + s + "\"" + (radix != 10 ? " under radix " + radix : ""));
            long next = result * radix - d;
            if (result < Long.MIN_VALUE / radix || next > result) overflow = true;
            result = next;
        }
        if (!neg) { if (result == Long.MIN_VALUE) overflow = true; result = -result; }
        if (overflow || result < min || result > max) throw new NumberFormatException("For input string: \"" + s + "\"" + (radix != 10 ? " under radix " + radix : ""));
        return result;
    }

    public static String toString(long v) { return toString(v, 10); }
    public static String toString(long v, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (v == 0) return "0";
        char[] buf = new char[65];
        int at = 65;
        boolean neg = v < 0;
        if (!neg) v = -v;
        while (v != 0) { buf[--at] = Character.forDigit((int) -(v % radix), radix); v /= radix; }
        if (neg) buf[--at] = '-';
        return new String(buf, at, 65 - at);
    }
    static String toUnsigned(long v, int shift) {
        char[] buf = new char[64];
        int at = 64, mask = (1 << shift) - 1;
        do { buf[--at] = Character.forDigit((int) (v & mask), 1 << shift); v >>>= shift; } while (v != 0);
        return new String(buf, at, 64 - at);
    }
    public static String toHexString(long v) { return toUnsigned(v, 4); }
    public static String toOctalString(long v) { return toUnsigned(v, 3); }
    public static String toBinaryString(long v) { return toUnsigned(v, 1); }
    public static int bitCount(long i) { return Integer.bitCount((int) i) + Integer.bitCount((int) (i >>> 32)); }
    public static int numberOfLeadingZeros(long i) { int h = (int) (i >>> 32); return h != 0 ? Integer.numberOfLeadingZeros(h) : 32 + Integer.numberOfLeadingZeros((int) i); }
    public static int numberOfTrailingZeros(long i) { int l = (int) i; return l != 0 ? Integer.numberOfTrailingZeros(l) : 32 + Integer.numberOfTrailingZeros((int) (i >>> 32)); }
    public static long highestOneBit(long i) { return i == 0 ? 0 : 1L << (63 - numberOfLeadingZeros(i)); }
    public static long lowestOneBit(long i) { return i & -i; }
    public static long rotateLeft(long i, int d) { return (i << d) | (i >>> -d); }
    public static long rotateRight(long i, int d) { return (i >>> d) | (i << -d); }
    public static long reverseBytes(long i) { return (long) Integer.reverseBytes((int) i) << 32 | (Integer.reverseBytes((int) (i >>> 32)) & 0xffffffffL); }
    public static int signum(long i) { return i < 0 ? -1 : i > 0 ? 1 : 0; }
    public static int compare(long a, long b) { return a < b ? -1 : a == b ? 0 : 1; }
    public static int hashCode(long v) { return (int) (v ^ (v >>> 32)); }
    public static long sum(long a, long b) { return a + b; }
    public static long max(long a, long b) { return Math.max(a, b); }
    public static long min(long a, long b) { return Math.min(a, b); }
    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int hashCode() { return hashCode(value); }
    public boolean equals(Object o) { return o instanceof Long && ((Long) o).value == value; }
    public int compareTo(Long o) { return compare(value, o.value); }
    public String toString() { return toString(value); }
}
