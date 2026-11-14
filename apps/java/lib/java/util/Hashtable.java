package java.util;

public class Hashtable<K, V> extends Dictionary<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    private HashMap<K, V> map;

    public Hashtable() { map = new HashMap<K, V>(); }
    public Hashtable(int capacity) { map = new HashMap<K, V>(capacity); }
    public Hashtable(int capacity, float load) { map = new HashMap<K, V>(capacity, load); }
    public Hashtable(Map<? extends K, ? extends V> m) { map = new HashMap<K, V>(); putAll(m); }
    public synchronized int size() { return map.size(); }
    public synchronized boolean isEmpty() { return map.isEmpty(); }
    public synchronized Enumeration<K> keys() { return Collections.enumeration(new ArrayList<K>(map.keySet())); }
    public synchronized Enumeration<V> elements() { return Collections.enumeration(new ArrayList<V>(map.values())); }
    public synchronized boolean contains(Object v) { return map.containsValue(v); }
    public boolean containsValue(Object v) { return contains(v); }
    public synchronized boolean containsKey(Object k) { return map.containsKey(k); }
    public synchronized V get(Object k) { return map.get(k); }
    public synchronized V put(K k, V v) { if (k == null || v == null) throw new NullPointerException(); return map.put(k, v); }
    public synchronized V remove(Object k) { return map.remove(k); }
    public synchronized void putAll(Map<? extends K, ? extends V> m) { for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) put(e.getKey(), e.getValue()); }
    public synchronized void clear() { map.clear(); }
    @SuppressWarnings("unchecked")
    public synchronized Object clone() { Hashtable<K, V> h = new Hashtable<K, V>(); h.map = (HashMap<K, V>) map.clone(); return h; }
    public synchronized String toString() { return map.toString(); }
    public Set<K> keySet() { return map.keySet(); }
    public Set<Map.Entry<K, V>> entrySet() { return map.entrySet(); }
    public Collection<V> values() { return map.values(); }
    public synchronized boolean equals(Object o) { return o == this || map.equals(o); }
    public synchronized int hashCode() { return map.hashCode(); }
    protected void rehash() {}
}
