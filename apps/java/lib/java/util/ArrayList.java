package java.util;

public class ArrayList<E> extends AbstractList<E> implements List<E>, RandomAccess, Cloneable, java.io.Serializable {
    private Object[] data;
    private int size;

    public ArrayList() { data = new Object[10]; }
    public ArrayList(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("Illegal Capacity: " + capacity);
        data = new Object[capacity];
    }
    public ArrayList(Collection<? extends E> c) { data = c.toArray(); size = data.length; if (data.getClass() != Object[].class) data = Arrays.copyOf(data, size, Object[].class); }
    public void ensureCapacity(int n) { if (n > data.length) { int g = data.length + (data.length >> 1) + 1; data = Arrays.copyOf(data, Math.max(g, n)); } }
    public void trimToSize() { modCount++; if (size < data.length) data = Arrays.copyOf(data, size); }
    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public boolean contains(Object o) { return indexOf(o) >= 0; }
    public int indexOf(Object o) {
        Object[] d = data;
        if (o == null) { for (int i = 0; i < size; i++) if (d[i] == null) return i; }
        else for (int i = 0; i < size; i++) if (o.equals(d[i])) return i;
        return -1;
    }
    public int lastIndexOf(Object o) {
        for (int i = size - 1; i >= 0; i--) if (o == null ? data[i] == null : o.equals(data[i])) return i;
        return -1;
    }
    @SuppressWarnings("unchecked")
    public Object clone() {
        try { ArrayList<E> c = (ArrayList<E>) super.clone(); c.data = Arrays.copyOf(data, size); c.modCount = 0; return c; }
        catch (CloneNotSupportedException e) { throw new InternalError(); }
    }
    public Object[] toArray() { return Arrays.copyOf(data, size); }
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        if (a.length < size) return (T[]) Arrays.copyOf(data, size, a.getClass());
        System.arraycopy(data, 0, a, 0, size);
        if (a.length > size) a[size] = null;
        return a;
    }
    private void check(int i) { if (i >= size || i < 0) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size); }
    @SuppressWarnings("unchecked")
    public E get(int i) { if (i >= size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size); return (E) data[i]; }
    @SuppressWarnings("unchecked")
    public E set(int i, E e) { check(i); E old = (E) data[i]; data[i] = e; return old; }
    public boolean add(E e) {
        modCount++;
        if (size == data.length) ensureCapacity(size + 1);
        data[size++] = e;
        return true;
    }
    public void add(int i, E e) {
        if (i > size || i < 0) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size);
        modCount++;
        if (size == data.length) ensureCapacity(size + 1);
        System.arraycopy(data, i, data, i + 1, size - i);
        data[i] = e;
        size++;
    }
    @SuppressWarnings("unchecked")
    public E remove(int i) {
        check(i);
        modCount++;
        E old = (E) data[i];
        int moved = size - i - 1;
        if (moved > 0) System.arraycopy(data, i + 1, data, i, moved);
        data[--size] = null;
        return old;
    }
    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i < 0) return false;
        remove(i);
        return true;
    }
    public void clear() { modCount++; for (int i = 0; i < size; i++) data[i] = null; size = 0; }
    public boolean addAll(Collection<? extends E> c) {
        Object[] a = c.toArray();
        modCount++;
        ensureCapacity(size + a.length);
        System.arraycopy(a, 0, data, size, a.length);
        size += a.length;
        return a.length != 0;
    }
    public boolean addAll(int i, Collection<? extends E> c) {
        if (i > size || i < 0) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size);
        Object[] a = c.toArray();
        modCount++;
        ensureCapacity(size + a.length);
        System.arraycopy(data, i, data, i + a.length, size - i);
        System.arraycopy(a, 0, data, i, a.length);
        size += a.length;
        return a.length != 0;
    }
    protected void removeRange(int from, int to) {
        modCount++;
        System.arraycopy(data, to, data, from, size - to);
        for (int i = size - (to - from); i < size; i++) data[i] = null;
        size -= to - from;
    }
    public boolean removeAll(Collection<?> c) { return batch(c, false); }
    public boolean retainAll(Collection<?> c) { return batch(c, true); }
    private boolean batch(Collection<?> c, boolean keep) {
        int w = 0;
        for (int r = 0; r < size; r++) if (c.contains(data[r]) == keep) data[w++] = data[r];
        if (w == size) return false;
        for (int i = w; i < size; i++) data[i] = null;
        modCount++;
        size = w;
        return true;
    }
    public Iterator<E> iterator() { return new Itr(); }
    private class Itr implements Iterator<E> {
        int cursor, last = -1, expected = modCount;
        public boolean hasNext() { return cursor != size; }
        @SuppressWarnings("unchecked")
        public E next() {
            if (modCount != expected) throw new ConcurrentModificationException();
            int i = cursor;
            if (i >= size) throw new NoSuchElementException();
            cursor = i + 1;
            return (E) data[last = i];
        }
        public void remove() {
            if (last < 0) throw new IllegalStateException();
            if (modCount != expected) throw new ConcurrentModificationException();
            ArrayList.this.remove(last);
            cursor = last;
            last = -1;
            expected = modCount;
        }
    }
}
