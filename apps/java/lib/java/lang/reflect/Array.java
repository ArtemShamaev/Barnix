package java.lang.reflect;

public final class Array {
    private Array() {}
    private static native Object newArray0(Class<?> component, int length);
    public static Object newInstance(Class<?> component, int length) throws NegativeArraySizeException {
        if (component == null) throw new NullPointerException();
        if (component == Void.TYPE) throw new IllegalArgumentException();
        return newArray0(component, length);
    }
    public static Object newInstance(Class<?> component, int... dims) {
        if (dims.length == 1) return newInstance(component, dims[0]);
        Class<?> inner = newInstance(component, 0).getClass();
        for (int i = 2; i < dims.length; i++) inner = newInstance(inner, 0).getClass();
        Object[] r = (Object[]) newInstance(inner, dims[0]);
        int[] rest = new int[dims.length - 1];
        System.arraycopy(dims, 1, rest, 0, rest.length);
        for (int i = 0; i < r.length; i++) r[i] = newInstance(component, rest);
        return r;
    }
    public static native int getLength(Object array);
    public static native Object get(Object array, int index);
    public static native void set(Object array, int index, Object value);
    public static int getInt(Object a, int i) { return ((Number) get(a, i)).intValue(); }
}
