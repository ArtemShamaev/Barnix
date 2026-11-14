package org.lwjgl;

import java.nio.*;

public final class BufferUtils {
    private BufferUtils() {}
    public static ByteBuffer createByteBuffer(int n) { return ByteBuffer.allocateDirect(n).order(ByteOrder.nativeOrder()); }
    public static ShortBuffer createShortBuffer(int n) { return createByteBuffer(n * 2).asShortBuffer(); }
    public static CharBuffer createCharBuffer(int n) { return createByteBuffer(n * 2).asCharBuffer(); }
    public static IntBuffer createIntBuffer(int n) { return createByteBuffer(n * 4).asIntBuffer(); }
    public static LongBuffer createLongBuffer(int n) { return createByteBuffer(n * 8).asLongBuffer(); }
    public static FloatBuffer createFloatBuffer(int n) { return createByteBuffer(n * 4).asFloatBuffer(); }
    public static DoubleBuffer createDoubleBuffer(int n) { return createByteBuffer(n * 8).asDoubleBuffer(); }
    public static int getElementSizeExponent(Buffer b) {
        if (b instanceof ByteBuffer) return 0;
        if (b instanceof ShortBuffer || b instanceof CharBuffer) return 1;
        if (b instanceof IntBuffer || b instanceof FloatBuffer) return 2;
        return 3;
    }
    public static void zeroBuffer(ByteBuffer b) { for (int i = b.position(); i < b.limit(); i++) b.put(i, (byte) 0); }
}
