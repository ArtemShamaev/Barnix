package java.util;

/* Sorted arrays with binary search: O(log n) lookups. Entries occupy
 * keys[start, start + size), and an update moves the shorter side, so taking
 * the first or last entry (a scheduler queue) costs no copying. */
public class TreeMap<K, V> extends AbstractMap<K, V> implements NavigableMap<K, V>, Cloneable, java.io.Serializable {
    private final Comparator<? super K> comparator;
    private Object[] keys = new Object[8], values = new Object[8];
    private int start, size, modCount;

    public TreeMap() { comparator = null; }
    public TreeMap(Comparator<? super K> c) { comparator = c; }
    public TreeMap(Map<? extends K, ? extends V> m) { comparator = null; putAll(m); }
    @SuppressWarnings("unchecked")
    public TreeMap(SortedMap<K, ? extends V> m) { comparator = m.comparator(); putAll(m); }

    @SuppressWarnings("unchecked")
    private int compare(Object a, Object b) { return comparator != null ? comparator.compare((K) a, (K) b) : ((Comparable<Object>) a).compareTo(b); }
    /* index of key, or -(insertion point) - 1 */
    private int find(Object key) {
        if (key == null && comparator == null) throw new NullPointerException();
        int lo = 0, hi = size - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1, c = compare(keys[start + mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public Comparator<? super K> comparator() { return comparator; }
    public int size() { return size; }
    public boolean containsKey(Object key) { return find(key) >= 0; }
    @SuppressWarnings("unchecked")
    public V get(Object key) { int i = find(key); return i >= 0 ? (V) values[start + i] : null; }
    @SuppressWarnings("unchecked")
    public V put(K key, V value) {
        if (size == 0 && key != null) compare(key, key);
        int i = find(key);
        if (i >= 0) { V old = (V) values[start + i]; values[start + i] = value; return old; }
        i = -i - 1;
        boolean front = start > 0 && (i < size / 2 || start + size == keys.length);
        if (!front && start + size == keys.length) {
            /* Full: double the room and centre the entries in it. */
            Object[] k = new Object[keys.length * 2], v = new Object[keys.length * 2];
            int s = (k.length - size) / 2;
            System.arraycopy(keys, start, k, s, size);
            System.arraycopy(values, start, v, s, size);
            keys = k; values = v; start = s;
            front = i < size / 2;
        }
        if (front) {
            System.arraycopy(keys, start, keys, start - 1, i);
            System.arraycopy(values, start, values, start - 1, i);
            start--;
        } else {
            System.arraycopy(keys, start + i, keys, start + i + 1, size - i);
            System.arraycopy(values, start + i, values, start + i + 1, size - i);
        }
        keys[start + i] = key; values[start + i] = value;
        size++; modCount++;
        return null;
    }
    @SuppressWarnings("unchecked")
    V removeAt(int i) {
        V old = (V) values[start + i];
        if (i < size / 2) {
            System.arraycopy(keys, start, keys, start + 1, i);
            System.arraycopy(values, start, values, start + 1, i);
            keys[start] = values[start] = null;
            start++;
        } else {
            System.arraycopy(keys, start + i + 1, keys, start + i, size - i - 1);
            System.arraycopy(values, start + i + 1, values, start + i, size - i - 1);
            keys[start + size - 1] = values[start + size - 1] = null;
        }
        size--;
        if (size == 0) start = 0;
        modCount++;
        return old;
    }
    public V remove(Object key) { int i = find(key); return i >= 0 ? removeAt(i) : null; }
    public void clear() { Arrays.fill(keys, start, start + size, null); Arrays.fill(values, start, start + size, null); start = size = 0; modCount++; }
    @SuppressWarnings("unchecked")
    private K key(int i) { if (i < 0 || i >= size) return null; return (K) keys[start + i]; }
    @SuppressWarnings("unchecked")
    private Map.Entry<K, V> entry(int i) { return i < 0 || i >= size ? null : new AbstractMap.SimpleImmutableEntry<K, V>((K) keys[start + i], (V) values[start + i]); }
    public K firstKey() { if (size == 0) throw new NoSuchElementException(); return key(0); }
    public K lastKey() { if (size == 0) throw new NoSuchElementException(); return key(size - 1); }
    public Map.Entry<K, V> firstEntry() { return entry(0); }
    public Map.Entry<K, V> lastEntry() { return entry(size - 1); }
    public Map.Entry<K, V> pollFirstEntry() { Map.Entry<K, V> e = entry(0); if (e != null) removeAt(0); return e; }
    public Map.Entry<K, V> pollLastEntry() { Map.Entry<K, V> e = entry(size - 1); if (e != null) removeAt(size - 1); return e; }
    private int floorIndex(Object k) { int i = find(k); return i >= 0 ? i : -i - 2; }
    private int ceilingIndex(Object k) { int i = find(k); return i >= 0 ? i : -i - 1; }
    private int lowerIndex(Object k) { int i = find(k); return i >= 0 ? i - 1 : -i - 2; }
    private int higherIndex(Object k) { int i = find(k); return i >= 0 ? i + 1 : -i - 1; }
    public Map.Entry<K, V> floorEntry(K k) { return entry(floorIndex(k)); }
    public Map.Entry<K, V> ceilingEntry(K k) { return entry(ceilingIndex(k)); }
    public Map.Entry<K, V> lowerEntry(K k) { return entry(lowerIndex(k)); }
    public Map.Entry<K, V> higherEntry(K k) { return entry(higherIndex(k)); }
    public K floorKey(K k) { return key(floorIndex(k)); }
    public K ceilingKey(K k) { return key(ceilingIndex(k)); }
    public K lowerKey(K k) { return key(lowerIndex(k)); }
    public K higherKey(K k) { return key(higherIndex(k)); }

    private SortedMap<K, V> range(Object from, boolean hasFrom, Object to, boolean hasTo) {
        TreeMap<K, V> m = new TreeMap<K, V>(comparator);
        int a = hasFrom ? ceilingIndex(from) : 0, b = hasTo ? ceilingIndex(to) : size;
        for (int i = a; i < b; i++) m.put(key(i), entry(i).getValue());
        return m;
    }
    public SortedMap<K, V> subMap(K from, K to) { return range(from, true, to, true); }
    public SortedMap<K, V> headMap(K to) { return range(null, false, to, true); }
    public SortedMap<K, V> tailMap(K from) { return range(from, true, null, false); }

    private abstract class Itr<T> implements Iterator<T> {
        int next, last = -1, expected = modCount;
        public boolean hasNext() { return next < size; }
        int advance() {
            if (modCount != expected) throw new ConcurrentModificationException();
            if (next >= size) throw new NoSuchElementException();
            return last = next++;
        }
        public void remove() {
            if (last < 0) throw new IllegalStateException();
            if (modCount != expected) throw new ConcurrentModificationException();
            removeAt(last);
            next = last;
            last = -1;
            expected = modCount;
        }
    }
    public NavigableSet<K> navigableKeySet() { return new TreeSet<K>(this, true); }
    public Set<K> keySet() { return navigableKeySet(); }
    Iterator<K> keyIterator() { return new Itr<K>() { public K next() { return key(advance()); } }; }
    Iterator<K> descendingKeyIterator() {
        return new Iterator<K>() {
            int i = size - 1, last = -1;
            public boolean hasNext() { return i >= 0; }
            public K next() { if (i < 0) throw new NoSuchElementException(); last = i; return key(i--); }
            public void remove() { if (last < 0) throw new IllegalStateException(); removeAt(last); last = -1; }
        };
    }
    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public int size() { return size; }
            @SuppressWarnings("unchecked")
            public Iterator<V> iterator() { return new Itr<V>() { public V next() { return (V) values[start + advance()]; } }; }
        };
    }
    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() { return size; }
            public Iterator<Map.Entry<K, V>> iterator() {
                return new Itr<Map.Entry<K, V>>() {
                    @SuppressWarnings("unchecked")
                    public Map.Entry<K, V> next() {
                        final int i = advance();
                        return new AbstractMap.SimpleEntry<K, V>((K) keys[start + i], (V) values[start + i]) {
                            public V setValue(V v) { values[start + i] = v; return super.setValue(v); }
                        };
                    }
                };
            }
        };
    }
    @SuppressWarnings("unchecked")
    public Object clone() { TreeMap<K, V> m = new TreeMap<K, V>(comparator); m.keys = keys.clone(); m.values = values.clone(); m.start = start; m.size = size; return m; }
}
