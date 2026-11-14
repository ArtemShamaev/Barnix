package java.util;

public class LinkedList<E> extends AbstractSequentialList<E> implements List<E>, Deque<E>, Cloneable, java.io.Serializable {
    private static final class Node<E> { E item; Node<E> next, prev; Node(Node<E> p, E e, Node<E> n) { prev = p; item = e; next = n; } }
    private transient int size;
    private transient Node<E> first, last;

    public LinkedList() {}
    public LinkedList(Collection<? extends E> c) { addAll(c); }

    private void linkLast(E e) {
        Node<E> l = last, n = new Node<E>(l, e, null);
        last = n;
        if (l == null) first = n; else l.next = n;
        size++; modCount++;
    }
    private void linkBefore(E e, Node<E> succ) {
        Node<E> p = succ.prev, n = new Node<E>(p, e, succ);
        succ.prev = n;
        if (p == null) first = n; else p.next = n;
        size++; modCount++;
    }
    private E unlink(Node<E> x) {
        E e = x.item;
        Node<E> n = x.next, p = x.prev;
        if (p == null) first = n; else { p.next = n; x.prev = null; }
        if (n == null) last = p; else { n.prev = p; x.next = null; }
        x.item = null;
        size--; modCount++;
        return e;
    }
    private Node<E> node(int i) {
        if (i < (size >> 1)) { Node<E> x = first; for (int k = 0; k < i; k++) x = x.next; return x; }
        Node<E> x = last;
        for (int k = size - 1; k > i; k--) x = x.prev;
        return x;
    }
    private void check(int i) { if (i < 0 || i >= size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size); }

    public E getFirst() { if (first == null) throw new NoSuchElementException(); return first.item; }
    public E getLast() { if (last == null) throw new NoSuchElementException(); return last.item; }
    public E removeFirst() { if (first == null) throw new NoSuchElementException(); return unlink(first); }
    public E removeLast() { if (last == null) throw new NoSuchElementException(); return unlink(last); }
    public void addFirst(E e) { if (first == null) linkLast(e); else linkBefore(e, first); }
    public void addLast(E e) { linkLast(e); }
    public boolean contains(Object o) { return indexOf(o) >= 0; }
    public int size() { return size; }
    public boolean add(E e) { linkLast(e); return true; }
    public boolean remove(Object o) {
        for (Node<E> x = first; x != null; x = x.next) if (o == null ? x.item == null : o.equals(x.item)) { unlink(x); return true; }
        return false;
    }
    public boolean addAll(Collection<? extends E> c) { for (E e : c) linkLast(e); return !c.isEmpty(); }
    public void clear() { first = last = null; size = 0; modCount++; }
    public E get(int i) { check(i); return node(i).item; }
    public E set(int i, E e) { check(i); Node<E> x = node(i); E old = x.item; x.item = e; return old; }
    public void add(int i, E e) {
        if (i < 0 || i > size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size);
        if (i == size) linkLast(e); else linkBefore(e, node(i));
    }
    public E remove(int i) { check(i); return unlink(node(i)); }
    public int indexOf(Object o) {
        int i = 0;
        for (Node<E> x = first; x != null; x = x.next, i++) if (o == null ? x.item == null : o.equals(x.item)) return i;
        return -1;
    }
    public int lastIndexOf(Object o) {
        int i = size - 1;
        for (Node<E> x = last; x != null; x = x.prev, i--) if (o == null ? x.item == null : o.equals(x.item)) return i;
        return -1;
    }
    public E peek() { return first == null ? null : first.item; }
    public E element() { return getFirst(); }
    public E poll() { return first == null ? null : unlink(first); }
    public E remove() { return removeFirst(); }
    public boolean offer(E e) { return add(e); }
    public boolean offerFirst(E e) { addFirst(e); return true; }
    public boolean offerLast(E e) { addLast(e); return true; }
    public E peekFirst() { return first == null ? null : first.item; }
    public E peekLast() { return last == null ? null : last.item; }
    public E pollFirst() { return first == null ? null : unlink(first); }
    public E pollLast() { return last == null ? null : unlink(last); }
    public void push(E e) { addFirst(e); }
    public E pop() { return removeFirst(); }
    public Object[] toArray() { Object[] r = new Object[size]; int i = 0; for (Node<E> x = first; x != null; x = x.next) r[i++] = x.item; return r; }
    @SuppressWarnings("unchecked")
    public Object clone() { LinkedList<E> c = new LinkedList<E>(); for (Node<E> x = first; x != null; x = x.next) c.add(x.item); return c; }

    public ListIterator<E> listIterator(int index) {
        if (index < 0 || index > size) throw new IndexOutOfBoundsException("Index: " + index);
        return new ListItr(index);
    }
    private class ListItr implements ListIterator<E> {
        private Node<E> lastReturned, next;
        private int nextIndex, expected = modCount;
        ListItr(int index) { next = index == size ? null : node(index); nextIndex = index; }
        public boolean hasNext() { return nextIndex < size; }
        public E next() {
            check();
            if (!hasNext()) throw new NoSuchElementException();
            lastReturned = next; next = next.next; nextIndex++;
            return lastReturned.item;
        }
        public boolean hasPrevious() { return nextIndex > 0; }
        public E previous() {
            check();
            if (!hasPrevious()) throw new NoSuchElementException();
            lastReturned = next = next == null ? last : next.prev;
            nextIndex--;
            return lastReturned.item;
        }
        public int nextIndex() { return nextIndex; }
        public int previousIndex() { return nextIndex - 1; }
        public void remove() {
            check();
            if (lastReturned == null) throw new IllegalStateException();
            Node<E> ln = lastReturned.next;
            unlink(lastReturned);
            if (next == lastReturned) next = ln; else nextIndex--;
            lastReturned = null;
            expected++;
        }
        public void set(E e) { if (lastReturned == null) throw new IllegalStateException(); check(); lastReturned.item = e; }
        public void add(E e) {
            check();
            lastReturned = null;
            if (next == null) linkLast(e); else linkBefore(e, next);
            nextIndex++;
            expected++;
        }
        void check() { if (modCount != expected) throw new ConcurrentModificationException(); }
    }
    public Iterator<E> descendingIterator() {
        return new Iterator<E>() {
            private final ListItr i = new ListItr(size);
            public boolean hasNext() { return i.hasPrevious(); }
            public E next() { return i.previous(); }
            public void remove() { i.remove(); }
        };
    }
}
