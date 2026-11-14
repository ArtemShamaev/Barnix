package java.util;

public interface SortedSet<E> extends Set<E> {
    Comparator<? super E> comparator();
    SortedSet<E> subSet(E from, E to);
    SortedSet<E> headSet(E to);
    SortedSet<E> tailSet(E from);
    E first();
    E last();
}
