package java.lang;

public class ThreadGroup {
    private final String name;
    private final ThreadGroup parent;
    public ThreadGroup(String name) { this(null, name); }
    public ThreadGroup(ThreadGroup parent, String name) { this.parent = parent; this.name = name; }
    public final String getName() { return name; }
    public final ThreadGroup getParent() { return parent; }
    public int activeCount() { return 0; }
    public void uncaughtException(Thread t, Throwable e) { e.printStackTrace(); }
}
