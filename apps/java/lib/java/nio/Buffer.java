package java.nio;

public abstract class Buffer {
    int mark = -1, position, limit, capacity;
    Object hb;          /* backing primitive array */
    int offset;         /* byte offset of element 0 in hb */
    boolean direct, readOnly;

    Buffer(int capacity, Object hb, int offset) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0: " + capacity);
        this.capacity = limit = capacity;
        this.hb = hb;
        this.offset = offset;
    }
    public final int capacity() { return capacity; }
    public final int position() { return position; }
    public Buffer position(int p) {
        if (p > limit || p < 0) throw new IllegalArgumentException("newPosition > limit: (" + p + " > " + limit + ")");
        position = p;
        if (mark > p) mark = -1;
        return this;
    }
    public final int limit() { return limit; }
    public Buffer limit(int l) {
        if (l > capacity || l < 0) throw new IllegalArgumentException("newLimit > capacity: (" + l + " > " + capacity + ")");
        limit = l;
        if (position > l) position = l;
        if (mark > l) mark = -1;
        return this;
    }
    public Buffer mark() { mark = position; return this; }
    public Buffer reset() { if (mark < 0) throw new InvalidMarkException(); position = mark; return this; }
    public Buffer clear() { position = 0; limit = capacity; mark = -1; return this; }
    public Buffer flip() { limit = position; position = 0; mark = -1; return this; }
    public Buffer rewind() { position = 0; mark = -1; return this; }
    public final int remaining() { return limit - position; }
    public final boolean hasRemaining() { return position < limit; }
    public boolean isReadOnly() { return readOnly; }
    public boolean isDirect() { return direct; }
    public abstract boolean hasArray();
    public abstract Object array();
    public abstract int arrayOffset();
    final int nextGet() { if (position >= limit) throw new BufferUnderflowException(); return position++; }
    final int nextGet(int n) { if (limit - position < n) throw new BufferUnderflowException(); int p = position; position += n; return p; }
    final int nextPut() { if (readOnly) throw new ReadOnlyBufferException(); if (position >= limit) throw new BufferOverflowException(); return position++; }
    final int nextPut(int n) { if (readOnly) throw new ReadOnlyBufferException(); if (limit - position < n) throw new BufferOverflowException(); int p = position; position += n; return p; }
    final int check(int i) { if (i < 0 || i >= limit) throw new IndexOutOfBoundsException(); return i; }
    final int check(int i, int n) { if (i < 0 || n > limit - i) throw new IndexOutOfBoundsException(); return i; }
    final void writable() { if (readOnly) throw new ReadOnlyBufferException(); }
}
