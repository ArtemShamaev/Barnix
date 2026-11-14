package java.util;

public class TreeSet<E> extends AbstractSet<E> implements NavigableSet<E>, Cloneable, java.io.Serializable {
    private final TreeMap<E, Object> map;
    private static final Object PRESENT = new Object();

    @SuppressWarnings("unchecked")
    TreeSet(TreeMap<E, ?> m, boolean view) { map = (TreeMap<E, Object>) m; }
    public TreeSet() { map = new TreeMap<E, Object>(); }
    public TreeSet(Comparator<? super E> c) { map = new TreeMap<E, Object>(c); }
    public TreeSet(Collection<? extends E> c) { this(); addAll(c); }
    public TreeSet(SortedSet<E> s) { this(s.comparator()); addAll(s); }
    public Iterator<E> iterator() { return map.keyIterator(); }
    public Iterator<E> descendingIterator() { return map.descendingKeyIterator(); }
    public int size() { return map.size(); }
    public boolean isEmpty() { return map.isEmpty(); }
    public boolean contains(Object o) { return map.containsKey(o); }
    public boolean add(E e) { return map.put(e, PRESENT) == null; }
    public boolean remove(Object o) { return map.remove(o) == PRESENT; }
    public void clear() { map.clear(); }
    public Comparator<? super E> comparator() { return map.comparator(); }
    public E first() { return map.firstKey(); }
    public E last() { return map.lastKey(); }
    public E lower(E e) { return map.lowerKey(e); }
    public E floor(E e) { return map.floorKey(e); }
    public E ceiling(E e) { return map.ceilingKey(e); }
    public E higher(E e) { return map.higherKey(e); }
    public E pollFirst() { Map.Entry<E, ?> e = map.pollFirstEntry(); return e == null ? null : e.getKey(); }
    public E pollLast() { Map.Entry<E, ?> e = map.pollLastEntry(); return e == null ? null : e.getKey(); }
    public SortedSet<E> subSet(E from, E to) { return new TreeSet<E>((TreeMap<E, Object>) map.subMap(from, to), true); }
    public SortedSet<E> headSet(E to) { return new TreeSet<E>((TreeMap<E, Object>) map.headMap(to), true); }
    public SortedSet<E> tailSet(E from) { return new TreeSet<E>((TreeMap<E, Object>) map.tailMap(from), true); }
    @SuppressWarnings("unchecked")
    public Object clone() { return new TreeSet<E>((TreeMap<E, Object>) map.clone(), true); }
}
