package java.lang;

public class Thread implements Runnable {
    public static final int MIN_PRIORITY = 1, NORM_PRIORITY = 5, MAX_PRIORITY = 10;
    private int vmThread;
    private String name;
    private int priority = NORM_PRIORITY;
    private boolean daemon;
    private volatile boolean alive;
    private boolean started;
    private Runnable target;
    private ThreadGroup group;
    private ClassLoader contextLoader;
    private UncaughtExceptionHandler handler;
    java.util.HashMap<ThreadLocal<?>, Object> locals;
    private static int counter;
    private static long ids;
    private final long id = ++ids;
    private static UncaughtExceptionHandler defaultHandler;

    public interface UncaughtExceptionHandler { void uncaughtException(Thread t, Throwable e); }

    public Thread() { init(null, null); }
    public Thread(Runnable target) { init(target, null); }
    public Thread(Runnable target, String name) { init(target, name); }
    public Thread(String name) { init(null, name); }
    public Thread(ThreadGroup g, Runnable target) { init(target, null); group = g; }
    public Thread(ThreadGroup g, Runnable target, String name) { init(target, name); group = g; }
    public Thread(ThreadGroup g, Runnable target, String name, long stack) { init(target, name); group = g; }
    public Thread(ThreadGroup g, String name) { init(null, name); group = g; }

    private void init(Runnable target, String name) {
        this.target = target;
        this.name = name != null ? name : "Thread-" + counter++;
        Thread parent = currentThread();
        if (parent != null) { daemon = parent.daemon; priority = parent.priority; group = parent.group; contextLoader = parent.contextLoader; }
    }

    public static native Thread currentThread();
    private native void start0();
    private static native void sleep0(long ms);
    public static native void yield();
    private native void interrupt0();
    private native boolean isInterrupted0(boolean clear);
    public static native boolean holdsLock(Object o);
    private native void stop0();

    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        start0();
    }
    public void run() { if (target != null) target.run(); }
    public static void sleep(long ms) throws InterruptedException {
        if (ms < 0) throw new IllegalArgumentException("timeout value is negative");
        sleep0(ms);
    }
    public static void sleep(long ms, int nanos) throws InterruptedException { sleep(ms + (nanos >= 500000 ? 1 : 0)); }
    public final boolean isAlive() { return alive; }
    public final void join() throws InterruptedException { join(0); }
    public final synchronized void join(long ms) throws InterruptedException {
        if (ms < 0) throw new IllegalArgumentException("timeout value is negative");
        if (ms == 0) { while (alive) wait(0); return; }
        long end = System.currentTimeMillis() + ms;
        while (alive) {
            long left = end - System.currentTimeMillis();
            if (left <= 0) break;
            wait(left);
        }
    }
    public final void join(long ms, int nanos) throws InterruptedException { join(ms + (nanos > 0 ? 1 : 0)); }
    public void interrupt() { interrupt0(); }
    public boolean isInterrupted() { return isInterrupted0(false); }
    public static boolean interrupted() { return currentThread().isInterrupted0(true); }
    @Deprecated public final void stop() { stop0(); }
    @Deprecated public final void suspend() {}
    @Deprecated public final void resume() {}
    public final void setPriority(int p) {
        if (p < MIN_PRIORITY || p > MAX_PRIORITY) throw new IllegalArgumentException();
        priority = p;
    }
    public final int getPriority() { return priority; }
    public final void setDaemon(boolean on) { if (alive) throw new IllegalThreadStateException(); daemon = on; }
    public final boolean isDaemon() { return daemon; }
    public final String getName() { return name; }
    public final synchronized void setName(String n) { if (n == null) throw new NullPointerException(); name = n; }
    public long getId() { return id; }
    public final ThreadGroup getThreadGroup() { return alive ? group : null; }
    public ClassLoader getContextClassLoader() { return contextLoader != null ? contextLoader : ClassLoader.getSystemClassLoader(); }
    public void setContextClassLoader(ClassLoader l) { contextLoader = l; }
    public StackTraceElement[] getStackTrace() { return new StackTraceElement[0]; }
    public static void dumpStack() { new Exception("Stack trace").printStackTrace(); }
    public void setUncaughtExceptionHandler(UncaughtExceptionHandler h) { handler = h; }
    public UncaughtExceptionHandler getUncaughtExceptionHandler() { return handler; }
    public static void setDefaultUncaughtExceptionHandler(UncaughtExceptionHandler h) { defaultHandler = h; }
    public static UncaughtExceptionHandler getDefaultUncaughtExceptionHandler() { return defaultHandler; }
    public static int activeCount() { return 1; }
    public String toString() { return "Thread[" + name + "," + priority + ",main]"; }

    static void throwUnchecked(Throwable t) { Thread.<RuntimeException>sneaky(t); }
    @SuppressWarnings("unchecked")
    private static <E extends Throwable> void sneaky(Throwable t) throws E { throw (E) t; }
}
