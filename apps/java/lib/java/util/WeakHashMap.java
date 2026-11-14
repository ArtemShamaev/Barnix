package java.util;

/* Holds its keys strongly: a simplification. */
public class WeakHashMap<K, V> extends HashMap<K, V> {
    public WeakHashMap() { super(); }
    public WeakHashMap(int capacity) { super(capacity); }
    public WeakHashMap(int capacity, float load) { super(capacity, load); }
    public WeakHashMap(Map<? extends K, ? extends V> m) { super(m); }
}
