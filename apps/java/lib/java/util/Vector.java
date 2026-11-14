package java.util;

public class Vector<E> extends AbstractList<E> implements List<E>, RandomAccess, Cloneable, java.io.Serializable {
    protected Object[] elementData;
    protected int elementCount, capacityIncrement;

    public Vector() { this(10); }
    public Vector(int capacity) { this(capacity, 0); }
    public Vector(int capacity, int increment) { elementData = new Object[capacity]; capacityIncrement = increment; }
    public Vector(Collection<? extends E> c) { elementData = c.toArray(); elementCount = elementData.length; }
    public synchronized void ensureCapacity(int n) {
        if (n > elementData.length) elementData = Arrays.copyOf(elementData, Math.max(n, capacityIncrement > 0 ? elementData.length + capacityIncrement : elementData.length * 2 + 1));
    }
    public synchronized int size() { return elementCount; }
    public synchronized boolean isEmpty() { return elementCount == 0; }
    public synchronized int capacity() { return elementData.length; }
    public synchronized void setSize(int n) { ensureCapacity(n); for (int i = n; i < elementCount; i++) elementData[i] = null; elementCount = n; modCount++; }
    @SuppressWarnings("unchecked")
    public synchronized E get(int i) { if (i >= elementCount) throw new ArrayIndexOutOfBoundsException(i); return (E) elementData[i]; }
    @SuppressWarnings("unchecked")
    public synchronized E set(int i, E e) { if (i >= elementCount) throw new ArrayIndexOutOfBoundsException(i); E o = (E) elementData[i]; elementData[i] = e; return o; }
    public synchronized boolean add(E e) { modCount++; ensureCapacity(elementCount + 1); elementData[elementCount++] = e; return true; }
    public synchronized void add(int i, E e) { insertElementAt(e, i); }
    @SuppressWarnings("unchecked")
    public synchronized E remove(int i) {
        if (i >= elementCount) throw new ArrayIndexOutOfBoundsException(i);
        E o = (E) elementData[i];
        System.arraycopy(elementData, i + 1, elementData, i, elementCount - i - 1);
        elementData[--elementCount] = null;
        modCount++;
        return o;
    }
    public synchronized boolean remove(Object o) { int i = indexOf(o); if (i < 0) return false; remove(i); return true; }
    public synchronized void clear() { for (int i = 0; i < elementCount; i++) elementData[i] = null; elementCount = 0; modCount++; }
    public synchronized int indexOf(Object o) { return indexOf(o, 0); }
    public synchronized int indexOf(Object o, int from) {
        for (int i = from; i < elementCount; i++) if (o == null ? elementData[i] == null : o.equals(elementData[i])) return i;
        return -1;
    }
    public synchronized boolean contains(Object o) { return indexOf(o) >= 0; }
    public synchronized void addElement(E e) { add(e); }
    public synchronized boolean removeElement(Object o) { return remove(o); }
    public synchronized void removeElementAt(int i) { remove(i); }
    public synchronized void removeAllElements() { clear(); }
    public synchronized void insertElementAt(E e, int i) {
        if (i > elementCount) throw new ArrayIndexOutOfBoundsException(i);
        ensureCapacity(elementCount + 1);
        System.arraycopy(elementData, i, elementData, i + 1, elementCount - i);
        elementData[i] = e;
        elementCount++;
        modCount++;
    }
    public synchronized void setElementAt(E e, int i) { set(i, e); }
    public synchronized E elementAt(int i) { return get(i); }
    public synchronized E firstElement() { if (elementCount == 0) throw new NoSuchElementException(); return get(0); }
    public synchronized E lastElement() { if (elementCount == 0) throw new NoSuchElementException(); return get(elementCount - 1); }
    public synchronized void copyInto(Object[] a) { System.arraycopy(elementData, 0, a, 0, elementCount); }
    public synchronized Object[] toArray() { return Arrays.copyOf(elementData, elementCount); }
    public Enumeration<E> elements() {
        return new Enumeration<E>() {
            int i;
            public boolean hasMoreElements() { return i < elementCount; }
            public E nextElement() { synchronized (Vector.this) { if (i < elementCount) return get(i++); } throw new NoSuchElementException(); }
        };
    }
    @SuppressWarnings("unchecked")
    public synchronized Object clone() { Vector<E> v = new Vector<E>(elementCount + 1); for (int i = 0; i < elementCount; i++) v.add((E) elementData[i]); return v; }
}
