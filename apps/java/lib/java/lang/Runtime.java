package java.lang;

public class Runtime {
    private static final Runtime runtime = new Runtime();
    private java.util.ArrayList<Thread> hooks = new java.util.ArrayList<Thread>();
    private Runtime() {}
    public static Runtime getRuntime() { return runtime; }
    public native long freeMemory();
    public native long totalMemory();
    public native long maxMemory();
    public native int availableProcessors();
    public void gc() { System.gc(); }
    public void runFinalization() {}
    public void exit(int status) { System.exit(status); }
    public void halt(int status) { hooks.clear(); System.exit(status); }
    public synchronized void addShutdownHook(Thread hook) { hooks.add(hook); }
    public synchronized boolean removeShutdownHook(Thread hook) { return hooks.remove(hook); }
    void runShutdownHooks() {
        java.util.ArrayList<Thread> list;
        synchronized (this) { list = hooks; hooks = new java.util.ArrayList<Thread>(); }
        for (Thread t : list) { try { t.run(); } catch (Throwable e) { e.printStackTrace(); } }
    }
    public Process exec(String command) throws java.io.IOException { throw new java.io.IOException("Cannot run program \"" + command + "\": processes are not supported"); }
    public Process exec(String[] command) throws java.io.IOException { throw new java.io.IOException("Cannot run program: processes are not supported"); }
}
