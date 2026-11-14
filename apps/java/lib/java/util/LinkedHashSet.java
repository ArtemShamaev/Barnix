package java.util;

public class LinkedHashSet<E> extends HashSet<E> implements Set<E>, Cloneable, java.io.Serializable {
    public LinkedHashSet() { super(); }
    public LinkedHashSet(int capacity) { super(capacity); }
    public LinkedHashSet(int capacity, float load) { super(capacity, load); }
    public LinkedHashSet(Collection<? extends E> c) { super(c); }
}
