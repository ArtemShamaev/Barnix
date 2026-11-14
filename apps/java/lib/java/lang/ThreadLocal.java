package java.lang;

public class ThreadLocal<T> {
    public ThreadLocal() {}
    protected T initialValue() { return null; }
    @SuppressWarnings("unchecked")
    public T get() {
        Thread t = Thread.currentThread();
        if (t.locals == null) t.locals = new java.util.HashMap<ThreadLocal<?>, Object>();
        if (!t.locals.containsKey(this)) { T v = initialValue(); t.locals.put(this, v); return v; }
        return (T) t.locals.get(this);
    }
    public void set(T value) {
        Thread t = Thread.currentThread();
        if (t.locals == null) t.locals = new java.util.HashMap<ThreadLocal<?>, Object>();
        t.locals.put(this, value);
    }
    public void remove() { Thread t = Thread.currentThread(); if (t.locals != null) t.locals.remove(this); }
}
