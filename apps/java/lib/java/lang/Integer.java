package java.lang;

public final class Integer extends Number implements Comparable<Integer> {
    public static final int MIN_VALUE = 0x80000000, MAX_VALUE = 0x7fffffff, SIZE = 32, BYTES = 4;
    @SuppressWarnings("unchecked")
    public static final Class<Integer> TYPE = (Class<Integer>) Class.getPrimitiveClass("int");
    private static final Integer[] cache = new Integer[256];
    private final int value;

    public Integer(int v) { value = v; }
    public Integer(String s) throws NumberFormatException { value = parseInt(s, 10); }
    public static Integer valueOf(int v) {
        if (v >= -128 && v < 128) { Integer c = cache[v + 128]; if (c == null) cache[v + 128] = c = new Integer(v); return c; }
        return new Integer(v);
    }
    public static Integer valueOf(String s) throws NumberFormatException { return valueOf(parseInt(s, 10)); }
    public static Integer valueOf(String s, int radix) throws NumberFormatException { return valueOf(parseInt(s, radix)); }
    public static int parseInt(String s) throws NumberFormatException { return parseInt(s, 10); }
    public static int parseInt(String s, int radix) throws NumberFormatException {
        long v = Long.parse(s, radix, MIN_VALUE, MAX_VALUE);
        return (int) v;
    }
    public static int parseUnsignedInt(String s) { return (int) Long.parse(s, 10, 0, 0xffffffffL); }
    public static Integer decode(String s) {
        boolean neg = s.startsWith("-");
        String b = neg || s.startsWith("+") ? s.substring(1) : s;
        int radix = 10;
        if (b.startsWith("0x") || b.startsWith("0X")) { radix = 16; b = b.substring(2); }
        else if (b.startsWith("#")) { radix = 16; b = b.substring(1); }
        else if (b.startsWith("0") && b.length() > 1) { radix = 8; b = b.substring(1); }
        return valueOf((int) Long.parse(neg ? "-" + b : b, radix, MIN_VALUE, MAX_VALUE));
    }
    public static Integer getInteger(String name) { return getInteger(name, null); }
    public static Integer getInteger(String name, Integer def) {
        String v = System.getProperty(name);
        if (v == null) return def;
        try { return decode(v); } catch (NumberFormatException e) { return def; }
    }
    public static String toString(int i) { return new StringBuilder(11).append(i).toString(); }
    public static String toString(int i, int radix) { return Long.toString(i, radix); }
    public static String toHexString(int i) { return Long.toUnsigned(i & 0xffffffffL, 4); }
    public static String toOctalString(int i) { return Long.toUnsigned(i & 0xffffffffL, 3); }
    public static String toBinaryString(int i) { return Long.toUnsigned(i & 0xffffffffL, 1); }
    public static String toUnsignedString(int i) { return Long.toString(i & 0xffffffffL); }
    public static int bitCount(int i) { i = i - ((i >>> 1) & 0x55555555); i = (i & 0x33333333) + ((i >>> 2) & 0x33333333); i = (i + (i >>> 4)) & 0x0f0f0f0f; return (i * 0x01010101) >>> 24; }
    public static int numberOfLeadingZeros(int i) { if (i == 0) return 32; int n = 0; while ((i & 0x80000000) == 0) { n++; i <<= 1; } return n; }
    public static int numberOfTrailingZeros(int i) { if (i == 0) return 32; int n = 0; while ((i & 1) == 0) { n++; i >>>= 1; } return n; }
    public static int highestOneBit(int i) { return i == 0 ? 0 : 1 << (31 - numberOfLeadingZeros(i)); }
    public static int lowestOneBit(int i) { return i & -i; }
    public static int reverse(int i) { int r = 0; for (int k = 0; k < 32; k++) { r = r << 1 | (i & 1); i >>>= 1; } return r; }
    public static int reverseBytes(int i) { return (i >>> 24) | (i >> 8 & 0xff00) | (i << 8 & 0xff0000) | (i << 24); }
    public static int rotateLeft(int i, int d) { return (i << d) | (i >>> -d); }
    public static int rotateRight(int i, int d) { return (i >>> d) | (i << -d); }
    public static int signum(int i) { return (i >> 31) | (-i >>> 31); }
    public static int compare(int a, int b) { return a < b ? -1 : a == b ? 0 : 1; }
    public static int compareUnsigned(int a, int b) { return compare(a + MIN_VALUE, b + MIN_VALUE); }
    public static int divideUnsigned(int a, int b) { return (int) ((a & 0xffffffffL) / (b & 0xffffffffL)); }
    public static int remainderUnsigned(int a, int b) { return (int) ((a & 0xffffffffL) % (b & 0xffffffffL)); }
    public static int sum(int a, int b) { return a + b; }
    public static int max(int a, int b) { return Math.max(a, b); }
    public static int min(int a, int b) { return Math.min(a, b); }
    public static int hashCode(int v) { return v; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Integer && ((Integer) o).value == value; }
    public int compareTo(Integer o) { return compare(value, o.value); }
    public String toString() { return toString(value); }
}
