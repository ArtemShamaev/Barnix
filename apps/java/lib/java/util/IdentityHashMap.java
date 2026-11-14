package java.util;

public class IdentityHashMap<K, V> extends AbstractMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    private static final class Key {
        final Object o;
        Key(Object o) { this.o = o; }
        public int hashCode() { return System.identityHashCode(o); }
        public boolean equals(Object x) { return x instanceof Key && ((Key) x).o == o; }
    }
    private final HashMap<Key, V> map = new HashMap<Key, V>();
    public IdentityHashMap() {}
    public IdentityHashMap(int expected) {}
    public int size() { return map.size(); }
    public V get(Object k) { return map.get(new Key(k)); }
    public boolean containsKey(Object k) { return map.containsKey(new Key(k)); }
    public V put(K k, V v) { return map.put(new Key(k), v); }
    public V remove(Object k) { return map.remove(new Key(k)); }
    public void clear() { map.clear(); }
    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() { return map.size(); }
            public Iterator<Map.Entry<K, V>> iterator() {
                final Iterator<Map.Entry<Key, V>> i = map.entrySet().iterator();
                return new Iterator<Map.Entry<K, V>>() {
                    public boolean hasNext() { return i.hasNext(); }
                    @SuppressWarnings("unchecked")
                    public Map.Entry<K, V> next() {
                        final Map.Entry<Key, V> e = i.next();
                        return new AbstractMap.SimpleEntry<K, V>((K) e.getKey().o, e.getValue()) {
                            public V setValue(V v) { super.setValue(v); return e.setValue(v); }
                        };
                    }
                    public void remove() { i.remove(); }
                };
            }
        };
    }
}
