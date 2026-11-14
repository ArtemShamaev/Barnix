package java.lang;

public class Object {
    public Object() {}
    public final native Class<?> getClass();
    public native int hashCode();
    public boolean equals(Object other) { return this == other; }
    protected native Object clone() throws CloneNotSupportedException;
    public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }
    public final native void notify();
    public final native void notifyAll();
    public final native void wait(long timeout) throws InterruptedException;
    public final void wait(long timeout, int nanos) throws InterruptedException {
        if (nanos < 0 || nanos > 999999) throw new IllegalArgumentException("nanosecond timeout value out of range");
        wait(timeout + (nanos > 0 ? 1 : 0));
    }
    public final void wait() throws InterruptedException { wait(0); }
    protected void finalize() throws Throwable {}
}
