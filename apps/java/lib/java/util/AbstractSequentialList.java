package java.util;

public abstract class AbstractSequentialList<E> extends AbstractList<E> {
    protected AbstractSequentialList() {}
    public abstract ListIterator<E> listIterator(int index);
    public E get(int index) { return listIterator(index).next(); }
    public Iterator<E> iterator() { return listIterator(0); }
}
