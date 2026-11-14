package java.nio;

/* Raw access to the bytes of primitive arrays, by byte offset. */
final class Memory {
    private Memory() {}
    static native int get(Object array, int offset, int size, boolean swap);
    static native void put(Object array, int offset, int size, int value, boolean swap);
    static native long getLong(Object array, int offset, boolean swap);
    static native void putLong(Object array, int offset, long value, boolean swap);
    static native void copy(Object src, int srcOffset, Object dst, int dstOffset, int bytes, int elementSize, boolean swap);
}
