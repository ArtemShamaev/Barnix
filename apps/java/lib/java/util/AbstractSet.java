package java.util;

public abstract class AbstractSet<E> extends AbstractCollection<E> implements Set<E> {
    protected AbstractSet() {}
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Set)) return false;
        Collection<?> c = (Collection<?>) o;
        return c.size() == size() && containsAll(c);
    }
    public int hashCode() { int h = 0; for (E e : this) if (e != null) h += e.hashCode(); return h; }
    public boolean removeAll(Collection<?> c) {
        boolean m = false;
        if (size() > c.size()) { for (Object o : c) if (remove(o)) m = true; }
        else for (Iterator<E> i = iterator(); i.hasNext();) if (c.contains(i.next())) { i.remove(); m = true; }
        return m;
    }
}
