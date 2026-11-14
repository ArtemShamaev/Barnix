package java.util;

public interface NavigableMap<K, V> extends SortedMap<K, V> {
    Map.Entry<K, V> firstEntry();
    Map.Entry<K, V> lastEntry();
    Map.Entry<K, V> pollFirstEntry();
    Map.Entry<K, V> pollLastEntry();
    Map.Entry<K, V> floorEntry(K key);
    Map.Entry<K, V> ceilingEntry(K key);
    Map.Entry<K, V> lowerEntry(K key);
    Map.Entry<K, V> higherEntry(K key);
    K floorKey(K key);
    K ceilingKey(K key);
    K lowerKey(K key);
    K higherKey(K key);
    NavigableSet<K> navigableKeySet();
}
