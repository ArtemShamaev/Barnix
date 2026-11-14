package java.lang;

public final class Short extends Number implements Comparable<Short> {
    public static final short MIN_VALUE = -32768, MAX_VALUE = 32767;
    public static final int SIZE = 16, BYTES = 16 / 8;
    @SuppressWarnings("unchecked")
    public static final Class<Short> TYPE = (Class<Short>) Class.getPrimitiveClass("short");
    private final short value;

    public Short(short v) { value = v; }
    public Short(String s) throws NumberFormatException { value = parseShort(s, 10); }
    public static Short valueOf(short v) { return new Short(v); }
    public static Short valueOf(String s) throws NumberFormatException { return new Short(parseShort(s, 10)); }
    public static Short valueOf(String s, int radix) throws NumberFormatException { return new Short(parseShort(s, radix)); }
    public static short parseShort(String s) throws NumberFormatException { return parseShort(s, 10); }
    public static short parseShort(String s, int radix) throws NumberFormatException { return (short) Long.parse(s, radix, MIN_VALUE, MAX_VALUE); }
    public static Short decode(String s) throws NumberFormatException {
        int v = Integer.decode(s).intValue();
        if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException("Value out of range: " + s);
        return valueOf((short) v);
    }
    public static String toString(short v) { return Integer.toString(v); }
    public static int compare(short a, short b) { return a - b; }
    public static int hashCode(short v) { return v; }
    public static int toUnsignedInt(short v) { return v & ((1 << 16) - 1); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public short shortValue() { return value; }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Short && ((Short) o).value == value; }
    public int compareTo(Short o) { return value - o.value; }
    public String toString() { return Integer.toString(value); }
}
