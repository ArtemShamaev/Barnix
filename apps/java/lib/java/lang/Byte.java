package java.lang;

public final class Byte extends Number implements Comparable<Byte> {
    public static final byte MIN_VALUE = -128, MAX_VALUE = 127;
    public static final int SIZE = 8, BYTES = 8 / 8;
    @SuppressWarnings("unchecked")
    public static final Class<Byte> TYPE = (Class<Byte>) Class.getPrimitiveClass("byte");
    private final byte value;

    public Byte(byte v) { value = v; }
    public Byte(String s) throws NumberFormatException { value = parseByte(s, 10); }
    public static Byte valueOf(byte v) { return new Byte(v); }
    public static Byte valueOf(String s) throws NumberFormatException { return new Byte(parseByte(s, 10)); }
    public static Byte valueOf(String s, int radix) throws NumberFormatException { return new Byte(parseByte(s, radix)); }
    public static byte parseByte(String s) throws NumberFormatException { return parseByte(s, 10); }
    public static byte parseByte(String s, int radix) throws NumberFormatException { return (byte) Long.parse(s, radix, MIN_VALUE, MAX_VALUE); }
    public static Byte decode(String s) throws NumberFormatException {
        int v = Integer.decode(s).intValue();
        if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException("Value out of range: " + s);
        return valueOf((byte) v);
    }
    public static String toString(byte v) { return Integer.toString(v); }
    public static int compare(byte a, byte b) { return a - b; }
    public static int hashCode(byte v) { return v; }
    public static int toUnsignedInt(byte v) { return v & ((1 << 8) - 1); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public byte byteValue() { return value; }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Byte && ((Byte) o).value == value; }
    public int compareTo(Byte o) { return value - o.value; }
    public String toString() { return Integer.toString(value); }
}
