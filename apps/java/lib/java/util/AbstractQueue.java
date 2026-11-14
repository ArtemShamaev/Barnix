package java.util;

public abstract class AbstractQueue<E> extends AbstractCollection<E> implements Queue<E> {
    protected AbstractQueue() {}
    public boolean add(E e) { if (offer(e)) return true; throw new IllegalStateException("Queue full"); }
    public E remove() { E e = poll(); if (e == null) throw new NoSuchElementException(); return e; }
    public E element() { E e = peek(); if (e == null) throw new NoSuchElementException(); return e; }
    public void clear() { while (poll() != null) {} }
}
