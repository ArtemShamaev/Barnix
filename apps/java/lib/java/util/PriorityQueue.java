package java.util;

public class PriorityQueue<E> extends AbstractQueue<E> implements java.io.Serializable {
    private Object[] heap = new Object[11];
    private int size, modCount;
    private final Comparator<? super E> comparator;

    public PriorityQueue() { comparator = null; }
    public PriorityQueue(int capacity) { comparator = null; heap = new Object[Math.max(1, capacity)]; }
    public PriorityQueue(int capacity, Comparator<? super E> c) { comparator = c; heap = new Object[Math.max(1, capacity)]; }
    public PriorityQueue(Comparator<? super E> c) { comparator = c; }
    public PriorityQueue(Collection<? extends E> c) { comparator = null; addAll(c); }
    @SuppressWarnings("unchecked")
    private int cmp(Object a, Object b) { return comparator != null ? comparator.compare((E) a, (E) b) : ((Comparable<Object>) a).compareTo(b); }
    public boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        if (size == heap.length) heap = Arrays.copyOf(heap, size * 2 + 2);
        int i = size++;
        while (i > 0) { int p = (i - 1) >>> 1; if (cmp(e, heap[p]) >= 0) break; heap[i] = heap[p]; i = p; }
        heap[i] = e;
        modCount++;
        return true;
    }
    @SuppressWarnings("unchecked")
    public E peek() { return size == 0 ? null : (E) heap[0]; }
    @SuppressWarnings("unchecked")
    private E removeAt(int k) {
        E r = (E) heap[k];
        Object x = heap[--size];
        heap[size] = null;
        modCount++;
        if (k == size) return r;
        int i = k;
        for (;;) {
            int c = 2 * i + 1;
            if (c >= size) break;
            if (c + 1 < size && cmp(heap[c + 1], heap[c]) < 0) c++;
            if (cmp(x, heap[c]) <= 0) break;
            heap[i] = heap[c]; i = c;
        }
        heap[i] = x;
        while (i > 0) { int p = (i - 1) >>> 1; if (cmp(heap[i], heap[p]) >= 0) break; Object t = heap[i]; heap[i] = heap[p]; heap[p] = t; i = p; }
        return r;
    }
    public E poll() { return size == 0 ? null : removeAt(0); }
    public int size() { return size; }
    public Comparator<? super E> comparator() { return comparator; }
    public boolean remove(Object o) { for (int i = 0; i < size; i++) if (o.equals(heap[i])) { removeAt(i); return true; } return false; }
    public void clear() { for (int i = 0; i < size; i++) heap[i] = null; size = 0; }
    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int i, last = -1, expected = modCount;
            public boolean hasNext() { return i < size; }
            @SuppressWarnings("unchecked")
            public E next() { if (expected != modCount) throw new ConcurrentModificationException(); if (i >= size) throw new NoSuchElementException(); last = i; return (E) heap[i++]; }
            public void remove() { if (last < 0) throw new IllegalStateException(); removeAt(last); i = last; last = -1; expected = modCount; }
        };
    }
}
