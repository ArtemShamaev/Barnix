package java.util;

public abstract class AbstractCollection<E> implements Collection<E> {
    protected AbstractCollection() {}
    public abstract Iterator<E> iterator();
    public abstract int size();
    public boolean isEmpty() { return size() == 0; }
    public boolean contains(Object o) {
        for (Iterator<E> i = iterator(); i.hasNext();) { E e = i.next(); if (o == null ? e == null : o.equals(e)) return true; }
        return false;
    }
    public Object[] toArray() {
        Object[] r = new Object[size()];
        int k = 0;
        for (Iterator<E> i = iterator(); i.hasNext() && k < r.length;) r[k++] = i.next();
        return k == r.length ? r : Arrays.copyOf(r, k);
    }
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        int n = size();
        T[] r = a.length >= n ? a : (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), n);
        int k = 0;
        for (Iterator<E> i = iterator(); i.hasNext() && k < n;) r[k++] = (T) i.next();
        if (r.length > k) r[k] = null;
        return r;
    }
    public boolean add(E e) { throw new UnsupportedOperationException(); }
    public boolean remove(Object o) {
        for (Iterator<E> i = iterator(); i.hasNext();) { E e = i.next(); if (o == null ? e == null : o.equals(e)) { i.remove(); return true; } }
        return false;
    }
    public boolean containsAll(Collection<?> c) { for (Object o : c) if (!contains(o)) return false; return true; }
    public boolean addAll(Collection<? extends E> c) { boolean m = false; for (E e : c) if (add(e)) m = true; return m; }
    public boolean removeAll(Collection<?> c) {
        boolean m = false;
        for (Iterator<E> i = iterator(); i.hasNext();) if (c.contains(i.next())) { i.remove(); m = true; }
        return m;
    }
    public boolean retainAll(Collection<?> c) {
        boolean m = false;
        for (Iterator<E> i = iterator(); i.hasNext();) if (!c.contains(i.next())) { i.remove(); m = true; }
        return m;
    }
    public void clear() { for (Iterator<E> i = iterator(); i.hasNext();) { i.next(); i.remove(); } }
    public String toString() {
        Iterator<E> i = iterator();
        if (!i.hasNext()) return "[]";
        StringBuilder b = new StringBuilder("[");
        for (;;) {
            E e = i.next();
            b.append(e == this ? "(this Collection)" : String.valueOf(e));
            if (!i.hasNext()) return b.append(']').toString();
            b.append(", ");
        }
    }
}
