package java.util;

public abstract class AbstractMap<K, V> implements Map<K, V> {
    protected AbstractMap() {}
    public abstract Set<Map.Entry<K, V>> entrySet();
    public int size() { return entrySet().size(); }
    public boolean isEmpty() { return size() == 0; }
    public boolean containsValue(Object v) {
        for (Map.Entry<K, V> e : entrySet()) if (v == null ? e.getValue() == null : v.equals(e.getValue())) return true;
        return false;
    }
    public boolean containsKey(Object k) {
        for (Map.Entry<K, V> e : entrySet()) if (k == null ? e.getKey() == null : k.equals(e.getKey())) return true;
        return false;
    }
    public V get(Object k) {
        for (Map.Entry<K, V> e : entrySet()) if (k == null ? e.getKey() == null : k.equals(e.getKey())) return e.getValue();
        return null;
    }
    public V put(K k, V v) { throw new UnsupportedOperationException(); }
    public V remove(Object k) {
        for (Iterator<Map.Entry<K, V>> i = entrySet().iterator(); i.hasNext();) {
            Map.Entry<K, V> e = i.next();
            if (k == null ? e.getKey() == null : k.equals(e.getKey())) { V v = e.getValue(); i.remove(); return v; }
        }
        return null;
    }
    public void putAll(Map<? extends K, ? extends V> m) { for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) put(e.getKey(), e.getValue()); }
    public void clear() { entrySet().clear(); }
    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public Iterator<K> iterator() {
                final Iterator<Map.Entry<K, V>> i = entrySet().iterator();
                return new Iterator<K>() {
                    public boolean hasNext() { return i.hasNext(); }
                    public K next() { return i.next().getKey(); }
                    public void remove() { i.remove(); }
                };
            }
            public int size() { return AbstractMap.this.size(); }
            public boolean contains(Object k) { return containsKey(k); }
        };
    }
    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public Iterator<V> iterator() {
                final Iterator<Map.Entry<K, V>> i = entrySet().iterator();
                return new Iterator<V>() {
                    public boolean hasNext() { return i.hasNext(); }
                    public V next() { return i.next().getValue(); }
                    public void remove() { i.remove(); }
                };
            }
            public int size() { return AbstractMap.this.size(); }
        };
    }
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Map)) return false;
        Map<?, ?> m = (Map<?, ?>) o;
        if (m.size() != size()) return false;
        for (Map.Entry<K, V> e : entrySet()) {
            Object v = m.get(e.getKey());
            if (e.getValue() == null ? v != null || !m.containsKey(e.getKey()) : !e.getValue().equals(v)) return false;
        }
        return true;
    }
    public int hashCode() { int h = 0; for (Map.Entry<K, V> e : entrySet()) h += e.hashCode(); return h; }
    public String toString() {
        StringBuilder b = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<K, V> e : entrySet()) {
            if (!first) b.append(", ");
            first = false;
            b.append(e.getKey() == this ? "(this Map)" : String.valueOf(e.getKey())).append('=').append(e.getValue() == this ? "(this Map)" : String.valueOf(e.getValue()));
        }
        return b.append('}').toString();
    }

    public static class SimpleEntry<K, V> implements Map.Entry<K, V>, java.io.Serializable {
        private final K key;
        private V value;
        public SimpleEntry(K key, V value) { this.key = key; this.value = value; }
        public SimpleEntry(Map.Entry<? extends K, ? extends V> e) { key = e.getKey(); value = e.getValue(); }
        public K getKey() { return key; }
        public V getValue() { return value; }
        public V setValue(V v) { V old = value; value = v; return old; }
        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return (key == null ? e.getKey() == null : key.equals(e.getKey())) && (value == null ? e.getValue() == null : value.equals(e.getValue()));
        }
        public int hashCode() { return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode()); }
        public String toString() { return key + "=" + value; }
    }
    public static class SimpleImmutableEntry<K, V> extends SimpleEntry<K, V> {
        public SimpleImmutableEntry(K key, V value) { super(key, value); }
        public V setValue(V v) { throw new UnsupportedOperationException(); }
    }
}
