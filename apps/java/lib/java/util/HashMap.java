package java.util;

public class HashMap<K, V> extends AbstractMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    static class Node<K, V> implements Map.Entry<K, V> {
        final int hash;
        final K key;
        V value;
        Node<K, V> next;
        Node<K, V> before, after;   /* insertion order, used by LinkedHashMap */
        Node(int hash, K key, V value, Node<K, V> next) { this.hash = hash; this.key = key; this.value = value; this.next = next; }
        public final K getKey() { return key; }
        public final V getValue() { return value; }
        public final V setValue(V v) { V old = value; value = v; return old; }
        public final boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return (key == null ? e.getKey() == null : key.equals(e.getKey())) && (value == null ? e.getValue() == null : value.equals(e.getValue()));
        }
        public final int hashCode() { return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode()); }
        public final String toString() { return key + "=" + value; }
    }

    transient Node<K, V>[] table;
    transient int size, modCount;
    int threshold;
    final float loadFactor;
    transient Node<K, V> head, tail;
    boolean accessOrder;

    public HashMap() { this(16, 0.75f); }
    public HashMap(int capacity) { this(capacity, 0.75f); }
    @SuppressWarnings("unchecked")
    public HashMap(int capacity, float loadFactor) {
        if (capacity < 0) throw new IllegalArgumentException("Illegal initial capacity: " + capacity);
        if (!(loadFactor > 0)) throw new IllegalArgumentException("Illegal load factor: " + loadFactor);
        int n = 1;
        while (n < capacity) n <<= 1;
        this.loadFactor = loadFactor;
        table = (Node<K, V>[]) new Node[n];
        threshold = (int) (n * loadFactor);
    }
    public HashMap(Map<? extends K, ? extends V> m) { this(Math.max((int) (m.size() / 0.75f) + 1, 16), 0.75f); putAll(m); }

    static int hash(Object k) { if (k == null) return 0; int h = k.hashCode(); return h ^ (h >>> 16); }
    final Node<K, V> getNode(Object key) {
        int h = hash(key);
        for (Node<K, V> e = table[h & (table.length - 1)]; e != null; e = e.next) {
            Object k;
            if (e.hash == h && ((k = e.key) == key || (key != null && key.equals(k)))) return e;
        }
        return null;
    }
    public int size() { return size; }
    public boolean isEmpty() { return size == 0; }
    public V get(Object key) {
        Node<K, V> e = getNode(key);
        if (e == null) return null;
        if (accessOrder) touch(e);
        return e.value;
    }
    public boolean containsKey(Object key) { return getNode(key) != null; }
    public V put(K key, V value) {
        int h = hash(key), i = h & (table.length - 1);
        for (Node<K, V> e = table[i]; e != null; e = e.next) {
            Object k;
            if (e.hash == h && ((k = e.key) == key || (key != null && key.equals(k)))) {
                V old = e.value;
                e.value = value;
                if (accessOrder) touch(e);
                return old;
            }
        }
        Node<K, V> n = new Node<K, V>(h, key, value, table[i]);
        table[i] = n;
        if (tail == null) head = tail = n; else { tail.after = n; n.before = tail; tail = n; }
        modCount++;
        if (++size > threshold) resize();
        afterInsert();
        return null;
    }
    void afterInsert() {}
    private void touch(Node<K, V> e) {
        if (tail == e) return;
        unlinkOrder(e);
        e.before = tail; e.after = null;
        if (tail == null) head = e; else tail.after = e;
        tail = e;
        modCount++;
    }
    private void unlinkOrder(Node<K, V> e) {
        if (e.before == null) head = e.after; else e.before.after = e.after;
        if (e.after == null) tail = e.before; else e.after.before = e.before;
        e.before = e.after = null;
    }
    @SuppressWarnings("unchecked")
    private void resize() {
        Node<K, V>[] old = table, t = (Node<K, V>[]) new Node[old.length * 2];
        for (Node<K, V> e : old)
            while (e != null) { Node<K, V> next = e.next; int i = e.hash & (t.length - 1); e.next = t[i]; t[i] = e; e = next; }
        table = t;
        threshold = (int) (t.length * loadFactor);
    }
    final Node<K, V> removeNode(Object key) {
        int h = hash(key), i = h & (table.length - 1);
        Node<K, V> prev = null;
        for (Node<K, V> e = table[i]; e != null; prev = e, e = e.next) {
            Object k;
            if (e.hash == h && ((k = e.key) == key || (key != null && key.equals(k)))) {
                if (prev == null) table[i] = e.next; else prev.next = e.next;
                unlinkOrder(e);
                size--;
                modCount++;
                return e;
            }
        }
        return null;
    }
    public V remove(Object key) { Node<K, V> e = removeNode(key); return e == null ? null : e.value; }
    public void clear() { if (size > 0) { Arrays.fill(table, null); size = 0; head = tail = null; modCount++; } }
    public boolean containsValue(Object v) {
        for (Node<K, V> e = head; e != null; e = e.after) if (v == null ? e.value == null : v.equals(e.value)) return true;
        return false;
    }
    public void putAll(Map<? extends K, ? extends V> m) { for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) put(e.getKey(), e.getValue()); }
    @SuppressWarnings("unchecked")
    public Object clone() {
        HashMap<K, V> c = new HashMap<K, V>(table.length, loadFactor);
        c.accessOrder = accessOrder;
        for (Node<K, V> e = head; e != null; e = e.after) c.put(e.key, e.value);
        return c;
    }

    abstract class HashIterator<T> implements Iterator<T> {
        Node<K, V> next = head, current;
        int expected = modCount;
        public final boolean hasNext() { return next != null; }
        final Node<K, V> nextNode() {
            if (modCount != expected) throw new ConcurrentModificationException();
            Node<K, V> e = next;
            if (e == null) throw new NoSuchElementException();
            current = e;
            next = e.after;
            return e;
        }
        public final void remove() {
            if (current == null) throw new IllegalStateException();
            if (modCount != expected) throw new ConcurrentModificationException();
            removeNode(current.key);
            current = null;
            expected = modCount;
        }
    }
    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public Iterator<K> iterator() { return new HashIterator<K>() { public K next() { return nextNode().key; } }; }
            public int size() { return size; }
            public boolean contains(Object o) { return containsKey(o); }
            public boolean remove(Object o) { return removeNode(o) != null; }
            public void clear() { HashMap.this.clear(); }
        };
    }
    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public Iterator<V> iterator() { return new HashIterator<V>() { public V next() { return nextNode().value; } }; }
            public int size() { return size; }
            public boolean contains(Object o) { return containsValue(o); }
            public void clear() { HashMap.this.clear(); }
        };
    }
    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public Iterator<Map.Entry<K, V>> iterator() { return new HashIterator<Map.Entry<K, V>>() { public Map.Entry<K, V> next() { return nextNode(); } }; }
            public int size() { return size; }
            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                Node<K, V> n = getNode(e.getKey());
                return n != null && n.equals(e);
            }
            public boolean remove(Object o) { return contains(o) && removeNode(((Map.Entry<?, ?>) o).getKey()) != null; }
            public void clear() { HashMap.this.clear(); }
        };
    }
}
