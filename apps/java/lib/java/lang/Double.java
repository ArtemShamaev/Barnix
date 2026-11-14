package java.lang;

public final class Double extends Number implements Comparable<Double> {
    public static final double POSITIVE_INFINITY = 1.0 / 0.0, NEGATIVE_INFINITY = -1.0 / 0.0, NaN = 0.0d / 0.0;
    public static final double MAX_VALUE = 0x1.fffffffffffffP+1023, MIN_VALUE = 0x0.0000000000001P-1022, MIN_NORMAL = 0x1.0p-1022;
    public static final int SIZE = 64, BYTES = 8, MAX_EXPONENT = 1023, MIN_EXPONENT = -1022;
    @SuppressWarnings("unchecked")
    public static final Class<Double> TYPE = (Class<Double>) Class.getPrimitiveClass("double");
    private final double value;

    public Double(double v) { value = v; }
    public Double(String s) throws NumberFormatException { value = parseDouble(s); }
    public static Double valueOf(double v) { return new Double(v); }
    public static Double valueOf(String s) throws NumberFormatException { return new Double(parseDouble(s)); }
    public static double parseDouble(String s) throws NumberFormatException { return parse0(s); }
    private static native double parse0(String s);
    public static native String toString(double d);
    public static String toHexString(double d) {
        if (d != d) return "NaN";
        if (isInfinite(d)) return d > 0 ? "Infinity" : "-Infinity";
        long bits = doubleToRawLongBits(d);
        StringBuilder b = new StringBuilder();
        if (bits < 0) b.append('-');
        int exp = (int) (bits >>> 52 & 0x7ff);
        long mant = bits & 0xfffffffffffffL;
        if (exp == 0 && mant == 0) return b.append("0x0.0p0").toString();
        b.append(exp == 0 ? "0x0." : "0x1.");
        String h = Long.toHexString(mant | 1L << 52).substring(1);
        int end = h.length();
        while (end > 1 && h.charAt(end - 1) == '0') end--;
        b.append(h, 0, end).append('p').append(exp == 0 ? -1022 : exp - 1023);
        return b.toString();
    }
    public static native long doubleToRawLongBits(double d);
    public static long doubleToLongBits(double d) { return d != d ? 0x7ff8000000000000L : doubleToRawLongBits(d); }
    public static native double longBitsToDouble(long bits);
    public static boolean isNaN(double d) { return d != d; }
    public static boolean isInfinite(double d) { return d == POSITIVE_INFINITY || d == NEGATIVE_INFINITY; }
    public static boolean isFinite(double d) { return Math.abs(d) <= MAX_VALUE; }
    public static int compare(double a, double b) {
        if (a < b) return -1;
        if (a > b) return 1;
        long x = doubleToLongBits(a), y = doubleToLongBits(b);
        return x == y ? 0 : x < y ? -1 : 1;
    }
    public static int hashCode(double d) { long b = doubleToLongBits(d); return (int) (b ^ (b >>> 32)); }
    public static double sum(double a, double b) { return a + b; }
    public static double max(double a, double b) { return Math.max(a, b); }
    public static double min(double a, double b) { return Math.min(a, b); }
    public boolean isNaN() { return value != value; }
    public boolean isInfinite() { return isInfinite(value); }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return (float) value; }
    public double doubleValue() { return value; }
    public int hashCode() { return hashCode(value); }
    public boolean equals(Object o) { return o instanceof Double && doubleToLongBits(((Double) o).value) == doubleToLongBits(value); }
    public int compareTo(Double o) { return compare(value, o.value); }
    public String toString() { return toString(value); }
}
