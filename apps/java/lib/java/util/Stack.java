package java.util;

public class Stack<E> extends Vector<E> {
    public Stack() {}
    public E push(E e) { addElement(e); return e; }
    public synchronized E pop() { E e = peek(); remove(size() - 1); return e; }
    public synchronized E peek() { if (size() == 0) throw new EmptyStackException(); return get(size() - 1); }
    public boolean empty() { return size() == 0; }
    public synchronized int search(Object o) { int i = lastIndexOf(o); return i >= 0 ? size() - i : -1; }
}
