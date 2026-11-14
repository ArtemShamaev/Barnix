package java.util;

public abstract class AbstractList<E> extends AbstractCollection<E> implements List<E> {
    protected transient int modCount;
    protected AbstractList() {}
    public abstract E get(int index);
    public boolean add(E e) { add(size(), e); return true; }
    public E set(int index, E e) { throw new UnsupportedOperationException(); }
    public void add(int index, E e) { throw new UnsupportedOperationException(); }
    public E remove(int index) { throw new UnsupportedOperationException(); }
    public int indexOf(Object o) {
        for (int i = 0, n = size(); i < n; i++) { E e = get(i); if (o == null ? e == null : o.equals(e)) return i; }
        return -1;
    }
    public int lastIndexOf(Object o) {
        for (int i = size() - 1; i >= 0; i--) { E e = get(i); if (o == null ? e == null : o.equals(e)) return i; }
        return -1;
    }
    public void clear() { removeRange(0, size()); }
    protected void removeRange(int from, int to) { for (int i = to - 1; i >= from; i--) remove(i); }
    public boolean addAll(int index, Collection<? extends E> c) { for (E e : c) add(index++, e); return !c.isEmpty(); }
    public Iterator<E> iterator() { return new Itr(0); }
    public ListIterator<E> listIterator() { return new Itr(0); }
    public ListIterator<E> listIterator(int index) {
        if (index < 0 || index > size()) throw new IndexOutOfBoundsException("Index: " + index);
        return new Itr(index);
    }

    private class Itr implements ListIterator<E> {
        int cursor, last = -1, expected = modCount;
        Itr(int start) { cursor = start; }
        public boolean hasNext() { return cursor < size(); }
        public E next() {
            check();
            if (cursor >= size()) throw new NoSuchElementException();
            E e = get(cursor);
            last = cursor++;
            return e;
        }
        public boolean hasPrevious() { return cursor > 0; }
        public E previous() {
            check();
            if (cursor <= 0) throw new NoSuchElementException();
            E e = get(--cursor);
            last = cursor;
            return e;
        }
        public int nextIndex() { return cursor; }
        public int previousIndex() { return cursor - 1; }
        public void remove() {
            if (last < 0) throw new IllegalStateException();
            check();
            AbstractList.this.remove(last);
            if (last < cursor) cursor--;
            last = -1;
            expected = modCount;
        }
        public void set(E e) { if (last < 0) throw new IllegalStateException(); check(); AbstractList.this.set(last, e); expected = modCount; }
        public void add(E e) { check(); AbstractList.this.add(cursor++, e); last = -1; expected = modCount; }
        void check() { if (modCount != expected) throw new ConcurrentModificationException(); }
    }

    public List<E> subList(int from, int to) {
        if (from < 0 || to > size() || from > to) throw new IndexOutOfBoundsException();
        return new SubList<E>(this, from, to);
    }
    private static class SubList<E> extends AbstractList<E> {
        final AbstractList<E> parent;
        final int offset;
        int size;
        SubList(AbstractList<E> p, int from, int to) { parent = p; offset = from; size = to - from; modCount = p.modCount; }
        public E get(int i) { range(i); return parent.get(offset + i); }
        public E set(int i, E e) { range(i); return parent.set(offset + i, e); }
        public int size() { return size; }
        public void add(int i, E e) { if (i < 0 || i > size) throw new IndexOutOfBoundsException(); parent.add(offset + i, e); size++; modCount++; }
        public E remove(int i) { range(i); E e = parent.remove(offset + i); size--; modCount++; return e; }
        void range(int i) { if (i < 0 || i >= size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size); }
    }

    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof List)) return false;
        Iterator<E> a = iterator();
        Iterator<?> b = ((List<?>) o).iterator();
        while (a.hasNext() && b.hasNext()) { E x = a.next(); Object y = b.next(); if (!(x == null ? y == null : x.equals(y))) return false; }
        return !(a.hasNext() || b.hasNext());
    }
    public int hashCode() { int h = 1; for (E e : this) h = 31 * h + (e == null ? 0 : e.hashCode()); return h; }
}
