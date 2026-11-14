package java.util;

public interface SortedMap<K, V> extends Map<K, V> {
    Comparator<? super K> comparator();
    SortedMap<K, V> subMap(K from, K to);
    SortedMap<K, V> headMap(K to);
    SortedMap<K, V> tailMap(K from);
    K firstKey();
    K lastKey();
}
