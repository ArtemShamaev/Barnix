package java.util;

public class Collections {
    private Collections() {}
    @SuppressWarnings("rawtypes") public static final List EMPTY_LIST = new EmptyList<Object>();
    @SuppressWarnings("rawtypes") public static final Set EMPTY_SET = new EmptySet<Object>();
    @SuppressWarnings("rawtypes") public static final Map EMPTY_MAP = new EmptyMap<Object, Object>();

    private static class EmptyList<E> extends AbstractList<E> implements RandomAccess, java.io.Serializable {
        public int size() { return 0; }
        public E get(int i) { throw new IndexOutOfBoundsException("Index: " + i); }
    }
    private static class EmptySet<E> extends AbstractSet<E> implements java.io.Serializable {
        public int size() { return 0; }
        public Iterator<E> iterator() { return Collections.<E>emptyIterator(); }
    }
    private static class EmptyMap<K, V> extends AbstractMap<K, V> implements java.io.Serializable {
        public Set<Map.Entry<K, V>> entrySet() { return Collections.<Map.Entry<K, V>>emptySet(); }
    }
    @SuppressWarnings("unchecked") public static <T> List<T> emptyList() { return (List<T>) EMPTY_LIST; }
    @SuppressWarnings("unchecked") public static <T> Set<T> emptySet() { return (Set<T>) EMPTY_SET; }
    @SuppressWarnings("unchecked") public static <K, V> Map<K, V> emptyMap() { return (Map<K, V>) EMPTY_MAP; }
    public static <T> Iterator<T> emptyIterator() {
        return new Iterator<T>() {
            public boolean hasNext() { return false; }
            public T next() { throw new NoSuchElementException(); }
            public void remove() { throw new IllegalStateException(); }
        };
    }
    public static <T> Enumeration<T> emptyEnumeration() { return enumeration(Collections.<T>emptyList()); }

    @SuppressWarnings("unchecked")
    public static <T extends Comparable<? super T>> void sort(List<T> list) { sort(list, null); }
    @SuppressWarnings("unchecked")
    public static <T> void sort(List<T> list, Comparator<? super T> c) {
        Object[] a = list.toArray();
        Arrays.sort(a, (Comparator<Object>) c);
        ListIterator<T> i = list.listIterator();
        for (Object o : a) { i.next(); i.set((T) o); }
    }
    @SuppressWarnings("unchecked")
    public static <T> int binarySearch(List<? extends Comparable<? super T>> list, T key) {
        int lo = 0, hi = list.size() - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1, c = ((Comparable<T>) list.get(mid)).compareTo(key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static <T> int binarySearch(List<? extends T> list, T key, Comparator<? super T> c) {
        int lo = 0, hi = list.size() - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1, r = c.compare(list.get(mid), key);
            if (r < 0) lo = mid + 1; else if (r > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void reverse(List<?> list) { reverse0(list); }
    private static <T> void reverse0(List<T> list) {
        for (int i = 0, j = list.size() - 1; i < j; i++, j--) { T t = list.get(i); list.set(i, list.get(j)); list.set(j, t); }
    }
    public static void shuffle(List<?> list) { shuffle(list, new Random()); }
    public static void shuffle(List<?> list, Random r) { shuffle0(list, r); }
    private static <T> void shuffle0(List<T> list, Random r) {
        for (int i = list.size(); i > 1; i--) { int j = r.nextInt(i); T t = list.get(i - 1); list.set(i - 1, list.get(j)); list.set(j, t); }
    }
    public static void swap(List<?> list, int i, int j) { swap0(list, i, j); }
    private static <T> void swap0(List<T> l, int i, int j) { l.set(i, l.set(j, l.get(i))); }
    public static <T> void fill(List<? super T> list, T o) { for (int i = 0; i < list.size(); i++) list.set(i, o); }
    public static <T> void copy(List<? super T> dst, List<? extends T> src) { for (int i = 0; i < src.size(); i++) dst.set(i, src.get(i)); }
    public static <T extends Object & Comparable<? super T>> T min(Collection<? extends T> c) {
        Iterator<? extends T> i = c.iterator(); T m = i.next();
        while (i.hasNext()) { T x = i.next(); if (x.compareTo(m) < 0) m = x; }
        return m;
    }
    public static <T extends Object & Comparable<? super T>> T max(Collection<? extends T> c) {
        Iterator<? extends T> i = c.iterator(); T m = i.next();
        while (i.hasNext()) { T x = i.next(); if (x.compareTo(m) > 0) m = x; }
        return m;
    }
    public static <T> T min(Collection<? extends T> c, Comparator<? super T> cmp) {
        Iterator<? extends T> i = c.iterator(); T m = i.next();
        while (i.hasNext()) { T x = i.next(); if (cmp.compare(x, m) < 0) m = x; }
        return m;
    }
    public static <T> T max(Collection<? extends T> c, Comparator<? super T> cmp) {
        Iterator<? extends T> i = c.iterator(); T m = i.next();
        while (i.hasNext()) { T x = i.next(); if (cmp.compare(x, m) > 0) m = x; }
        return m;
    }
    public static int frequency(Collection<?> c, Object o) { int n = 0; for (Object x : c) if (o == null ? x == null : o.equals(x)) n++; return n; }
    @SafeVarargs
    public static <T> boolean addAll(Collection<? super T> c, T... elements) { boolean m = false; for (T e : elements) m |= c.add(e); return m; }
    public static <T> List<T> nCopies(final int n, final T o) {
        return new AbstractList<T>() { public int size() { return n; } public T get(int i) { if (i < 0 || i >= n) throw new IndexOutOfBoundsException(); return o; } };
    }
    public static <T> List<T> singletonList(T o) { List<T> l = new ArrayList<T>(1); l.add(o); return unmodifiableList(l); }
    public static <T> Set<T> singleton(T o) { Set<T> s = new HashSet<T>(); s.add(o); return unmodifiableSet(s); }
    public static <K, V> Map<K, V> singletonMap(K k, V v) { Map<K, V> m = new HashMap<K, V>(); m.put(k, v); return unmodifiableMap(m); }
    public static <T> Comparator<T> reverseOrder() {
        return new Comparator<T>() { @SuppressWarnings("unchecked") public int compare(T a, T b) { return ((Comparable<T>) b).compareTo(a); } };
    }
    public static <T> Comparator<T> reverseOrder(final Comparator<T> c) {
        if (c == null) return reverseOrder();
        return new Comparator<T>() { public int compare(T a, T b) { return c.compare(b, a); } };
    }
    public static <T> Enumeration<T> enumeration(final Collection<T> c) {
        return new Enumeration<T>() {
            final Iterator<T> i = c.iterator();
            public boolean hasMoreElements() { return i.hasNext(); }
            public T nextElement() { return i.next(); }
        };
    }
    public static <T> ArrayList<T> list(Enumeration<T> e) { ArrayList<T> l = new ArrayList<T>(); while (e.hasMoreElements()) l.add(e.nextElement()); return l; }

    static class UnmodifiableCollection<E> implements Collection<E>, java.io.Serializable {
        final Collection<? extends E> c;
        UnmodifiableCollection(Collection<? extends E> c) { if (c == null) throw new NullPointerException(); this.c = c; }
        public int size() { return c.size(); }
        public boolean isEmpty() { return c.isEmpty(); }
        public boolean contains(Object o) { return c.contains(o); }
        public Object[] toArray() { return c.toArray(); }
        public <T> T[] toArray(T[] a) { return c.toArray(a); }
        public String toString() { return c.toString(); }
        public Iterator<E> iterator() {
            return new Iterator<E>() {
                private final Iterator<? extends E> i = c.iterator();
                public boolean hasNext() { return i.hasNext(); }
                public E next() { return i.next(); }
                public void remove() { throw new UnsupportedOperationException(); }
            };
        }
        public boolean add(E e) { throw new UnsupportedOperationException(); }
        public boolean remove(Object o) { throw new UnsupportedOperationException(); }
        public boolean containsAll(Collection<?> x) { return c.containsAll(x); }
        public boolean addAll(Collection<? extends E> x) { throw new UnsupportedOperationException(); }
        public boolean removeAll(Collection<?> x) { throw new UnsupportedOperationException(); }
        public boolean retainAll(Collection<?> x) { throw new UnsupportedOperationException(); }
        public void clear() { throw new UnsupportedOperationException(); }
        public boolean equals(Object o) { return o == this || c.equals(o); }
        public int hashCode() { return c.hashCode(); }
    }
    static class UnmodifiableList<E> extends UnmodifiableCollection<E> implements List<E>, RandomAccess {
        final List<? extends E> list;
        UnmodifiableList(List<? extends E> l) { super(l); list = l; }
        public E get(int i) { return list.get(i); }
        public E set(int i, E e) { throw new UnsupportedOperationException(); }
        public void add(int i, E e) { throw new UnsupportedOperationException(); }
        public E remove(int i) { throw new UnsupportedOperationException(); }
        public int indexOf(Object o) { return list.indexOf(o); }
        public int lastIndexOf(Object o) { return list.lastIndexOf(o); }
        public boolean addAll(int i, Collection<? extends E> c) { throw new UnsupportedOperationException(); }
        public ListIterator<E> listIterator() { return listIterator(0); }
        public ListIterator<E> listIterator(final int index) {
            return new ListIterator<E>() {
                private final ListIterator<? extends E> i = list.listIterator(index);
                public boolean hasNext() { return i.hasNext(); }
                public E next() { return i.next(); }
                public boolean hasPrevious() { return i.hasPrevious(); }
                public E previous() { return i.previous(); }
                public int nextIndex() { return i.nextIndex(); }
                public int previousIndex() { return i.previousIndex(); }
                public void remove() { throw new UnsupportedOperationException(); }
                public void set(E e) { throw new UnsupportedOperationException(); }
                public void add(E e) { throw new UnsupportedOperationException(); }
            };
        }
        public List<E> subList(int a, int b) { return new UnmodifiableList<E>(list.subList(a, b)); }
    }
    static class UnmodifiableSet<E> extends UnmodifiableCollection<E> implements Set<E> { UnmodifiableSet(Set<? extends E> s) { super(s); } }
    public static <T> Collection<T> unmodifiableCollection(Collection<? extends T> c) { return new UnmodifiableCollection<T>(c); }
    public static <T> List<T> unmodifiableList(List<? extends T> l) { return new UnmodifiableList<T>(l); }
    public static <T> Set<T> unmodifiableSet(Set<? extends T> s) { return new UnmodifiableSet<T>(s); }
    public static <K, V> Map<K, V> unmodifiableMap(final Map<? extends K, ? extends V> m) {
        return new AbstractMap<K, V>() {
            @SuppressWarnings("unchecked")
            public Set<Map.Entry<K, V>> entrySet() { return unmodifiableSet((Set<Map.Entry<K, V>>) (Set<?>) m.entrySet()); }
            public V get(Object k) { return m.get(k); }
            public boolean containsKey(Object k) { return m.containsKey(k); }
            public int size() { return m.size(); }
        };
    }

    static class SynchronizedCollection<E> implements Collection<E>, java.io.Serializable {
        final Collection<E> c;
        final Object mutex;
        SynchronizedCollection(Collection<E> c) { this.c = c; mutex = this; }
        public int size() { synchronized (mutex) { return c.size(); } }
        public boolean isEmpty() { synchronized (mutex) { return c.isEmpty(); } }
        public boolean contains(Object o) { synchronized (mutex) { return c.contains(o); } }
        public Object[] toArray() { synchronized (mutex) { return c.toArray(); } }
        public <T> T[] toArray(T[] a) { synchronized (mutex) { return c.toArray(a); } }
        public Iterator<E> iterator() { return c.iterator(); }
        public boolean add(E e) { synchronized (mutex) { return c.add(e); } }
        public boolean remove(Object o) { synchronized (mutex) { return c.remove(o); } }
        public boolean containsAll(Collection<?> x) { synchronized (mutex) { return c.containsAll(x); } }
        public boolean addAll(Collection<? extends E> x) { synchronized (mutex) { return c.addAll(x); } }
        public boolean removeAll(Collection<?> x) { synchronized (mutex) { return c.removeAll(x); } }
        public boolean retainAll(Collection<?> x) { synchronized (mutex) { return c.retainAll(x); } }
        public void clear() { synchronized (mutex) { c.clear(); } }
        public String toString() { synchronized (mutex) { return c.toString(); } }
        public boolean equals(Object o) { synchronized (mutex) { return o == this || c.equals(o); } }
        public int hashCode() { synchronized (mutex) { return c.hashCode(); } }
    }
    static class SynchronizedList<E> extends SynchronizedCollection<E> implements List<E> {
        final List<E> list;
        SynchronizedList(List<E> l) { super(l); list = l; }
        public E get(int i) { synchronized (mutex) { return list.get(i); } }
        public E set(int i, E e) { synchronized (mutex) { return list.set(i, e); } }
        public void add(int i, E e) { synchronized (mutex) { list.add(i, e); } }
        public E remove(int i) { synchronized (mutex) { return list.remove(i); } }
        public int indexOf(Object o) { synchronized (mutex) { return list.indexOf(o); } }
        public int lastIndexOf(Object o) { synchronized (mutex) { return list.lastIndexOf(o); } }
        public boolean addAll(int i, Collection<? extends E> c) { synchronized (mutex) { return list.addAll(i, c); } }
        public ListIterator<E> listIterator() { return list.listIterator(); }
        public ListIterator<E> listIterator(int i) { return list.listIterator(i); }
        public List<E> subList(int a, int b) { synchronized (mutex) { return new SynchronizedList<E>(list.subList(a, b)); } }
    }
    static class SynchronizedSet<E> extends SynchronizedCollection<E> implements Set<E> { SynchronizedSet(Set<E> s) { super(s); } }
    public static <T> Collection<T> synchronizedCollection(Collection<T> c) { return new SynchronizedCollection<T>(c); }
    public static <T> List<T> synchronizedList(List<T> l) { return new SynchronizedList<T>(l); }
    public static <T> Set<T> synchronizedSet(Set<T> s) { return new SynchronizedSet<T>(s); }
    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> m) { return new Hashtable<K, V>() {
        private final Map<K, V> backing = m;
        public synchronized V put(K k, V v) { return backing.put(k, v); }
        public synchronized V get(Object k) { return backing.get(k); }
        public synchronized V remove(Object k) { return backing.remove(k); }
        public synchronized boolean containsKey(Object k) { return backing.containsKey(k); }
        public synchronized int size() { return backing.size(); }
        public synchronized boolean isEmpty() { return backing.isEmpty(); }
        public synchronized void clear() { backing.clear(); }
        public Set<K> keySet() { return backing.keySet(); }
        public Collection<V> values() { return backing.values(); }
        public Set<Map.Entry<K, V>> entrySet() { return backing.entrySet(); }
    }; }
}
