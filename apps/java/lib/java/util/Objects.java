package java.util;

public final class Objects {
    private Objects() {}
    public static boolean equals(Object a, Object b) { return a == b || (a != null && a.equals(b)); }
    public static boolean deepEquals(Object a, Object b) { return a == b || (a != null && b != null && Arrays.deepEquals(new Object[] {a}, new Object[] {b})); }
    public static int hashCode(Object o) { return o != null ? o.hashCode() : 0; }
    public static int hash(Object... values) { return Arrays.hashCode(values); }
    public static String toString(Object o) { return String.valueOf(o); }
    public static String toString(Object o, String def) { return o != null ? o.toString() : def; }
    public static <T> int compare(T a, T b, Comparator<? super T> c) { return a == b ? 0 : c.compare(a, b); }
    public static <T> T requireNonNull(T o) { if (o == null) throw new NullPointerException(); return o; }
    public static <T> T requireNonNull(T o, String message) { if (o == null) throw new NullPointerException(message); return o; }
    public static boolean isNull(Object o) { return o == null; }
    public static boolean nonNull(Object o) { return o != null; }
    public static <T> T requireNonNullElse(T o, T def) { return o != null ? o : requireNonNull(def, "defaultObj"); }
    public static int checkIndex(int i, int n) { if (i < 0 || i >= n) throw new IndexOutOfBoundsException("Index " + i + " out of bounds for length " + n); return i; }
}
