package java.util;

public class LinkedHashMap<K, V> extends HashMap<K, V> implements Map<K, V> {
    public LinkedHashMap() { super(); }
    public LinkedHashMap(int capacity) { super(capacity); }
    public LinkedHashMap(int capacity, float load) { super(capacity, load); }
    public LinkedHashMap(int capacity, float load, boolean accessOrder) { super(capacity, load); this.accessOrder = accessOrder; }
    public LinkedHashMap(Map<? extends K, ? extends V> m) { super(m); }
    protected boolean removeEldestEntry(Map.Entry<K, V> eldest) { return false; }
    void afterInsert() { if (head != null && removeEldestEntry(head)) removeNode(head.key); }
}
