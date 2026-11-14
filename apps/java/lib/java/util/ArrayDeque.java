package java.util;

public class ArrayDeque<E> extends AbstractCollection<E> implements Deque<E>, Cloneable, java.io.Serializable {
    private Object[] items = new Object[16];
    private int head, size;

    public ArrayDeque() {}
    public ArrayDeque(int n) { items = new Object[Math.max(4, n)]; }
    public ArrayDeque(Collection<? extends E> c) { this(c.size() + 1); addAll(c); }
    private void grow() {
        Object[] r = new Object[items.length * 2];
        for (int i = 0; i < size; i++) r[i] = items[(head + i) % items.length];
        items = r; head = 0;
    }
    public void addFirst(E e) { if (e == null) throw new NullPointerException(); if (size == items.length) grow(); head = (head - 1 + items.length) % items.length; items[head] = e; size++; }
    public void addLast(E e) { if (e == null) throw new NullPointerException(); if (size == items.length) grow(); items[(head + size) % items.length] = e; size++; }
    public boolean offerFirst(E e) { addFirst(e); return true; }
    public boolean offerLast(E e) { addLast(e); return true; }
    @SuppressWarnings("unchecked")
    public E pollFirst() { if (size == 0) return null; E e = (E) items[head]; items[head] = null; head = (head + 1) % items.length; size--; return e; }
    @SuppressWarnings("unchecked")
    public E pollLast() { if (size == 0) return null; int i = (head + size - 1) % items.length; E e = (E) items[i]; items[i] = null; size--; return e; }
    public E removeFirst() { E e = pollFirst(); if (e == null) throw new NoSuchElementException(); return e; }
    public E removeLast() { E e = pollLast(); if (e == null) throw new NoSuchElementException(); return e; }
    @SuppressWarnings("unchecked")
    public E peekFirst() { return size == 0 ? null : (E) items[head]; }
    @SuppressWarnings("unchecked")
    public E peekLast() { return size == 0 ? null : (E) items[(head + size - 1) % items.length]; }
    public E getFirst() { E e = peekFirst(); if (e == null) throw new NoSuchElementException(); return e; }
    public E getLast() { E e = peekLast(); if (e == null) throw new NoSuchElementException(); return e; }
    public boolean add(E e) { addLast(e); return true; }
    public boolean offer(E e) { addLast(e); return true; }
    public E remove() { return removeFirst(); }
    public E poll() { return pollFirst(); }
    public E element() { return getFirst(); }
    public E peek() { return peekFirst(); }
    public void push(E e) { addFirst(e); }
    public E pop() { return removeFirst(); }
    public int size() { return size; }
    public void clear() { for (int i = 0; i < items.length; i++) items[i] = null; head = size = 0; }
    private void removeAt(int k) {
        for (int i = k; i < size - 1; i++) items[(head + i) % items.length] = items[(head + i + 1) % items.length];
        items[(head + size - 1) % items.length] = null;
        size--;
    }
    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int i, last = -1;
            public boolean hasNext() { return i < size; }
            @SuppressWarnings("unchecked")
            public E next() { if (i >= size) throw new NoSuchElementException(); last = i; return (E) items[(head + i++) % items.length]; }
            public void remove() { if (last < 0) throw new IllegalStateException(); removeAt(last); i = last; last = -1; }
        };
    }
    public Iterator<E> descendingIterator() {
        return new Iterator<E>() {
            int i = size - 1, last = -1;
            public boolean hasNext() { return i >= 0; }
            @SuppressWarnings("unchecked")
            public E next() { if (i < 0) throw new NoSuchElementException(); last = i; return (E) items[(head + i--) % items.length]; }
            public void remove() { if (last < 0) throw new IllegalStateException(); removeAt(last); last = -1; }
        };
    }
}
