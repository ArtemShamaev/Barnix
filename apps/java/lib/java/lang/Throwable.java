package java.lang;

import java.io.PrintStream;
import java.io.PrintWriter;

public class Throwable implements java.io.Serializable {
    private transient Object backtrace;
    private String detailMessage;
    private Throwable cause = this;
    private StackTraceElement[] stackTrace;
    private Throwable[] suppressed;

    public Throwable() { fillInStackTrace(); }
    public Throwable(String message) { fillInStackTrace(); detailMessage = message; }
    public Throwable(String message, Throwable cause) { fillInStackTrace(); detailMessage = message; this.cause = cause; }
    public Throwable(Throwable cause) { fillInStackTrace(); detailMessage = cause == null ? null : cause.toString(); this.cause = cause; }
    protected Throwable(String message, Throwable cause, boolean suppression, boolean writable) { if (writable) fillInStackTrace(); detailMessage = message; this.cause = cause; }

    private native void fillInStackTrace0();
    private native StackTraceElement[] getStackTrace0();
    public synchronized Throwable fillInStackTrace() { fillInStackTrace0(); stackTrace = null; return this; }
    public String getMessage() { return detailMessage; }
    public String getLocalizedMessage() { return getMessage(); }
    public synchronized Throwable getCause() { return cause == this ? null : cause; }
    public synchronized Throwable initCause(Throwable c) {
        if (cause != this) throw new IllegalStateException("Can't overwrite cause", this);
        if (c == this) throw new IllegalArgumentException("Self-causation not permitted", this);
        cause = c;
        return this;
    }
    public String toString() {
        String m = getLocalizedMessage(), n = getClass().getName();
        return m != null ? n + ": " + m : n;
    }
    public StackTraceElement[] getStackTrace() { return getOurStackTrace().clone(); }
    private synchronized StackTraceElement[] getOurStackTrace() {
        if (stackTrace == null) stackTrace = backtrace != null ? getStackTrace0() : new StackTraceElement[0];
        return stackTrace;
    }
    public void setStackTrace(StackTraceElement[] t) { stackTrace = t.clone(); }
    public final synchronized void addSuppressed(Throwable t) {
        if (t == this) throw new IllegalArgumentException("Self-suppression not permitted", t);
        if (t == null) throw new NullPointerException("Cannot suppress a null exception.");
        Throwable[] s = suppressed == null ? new Throwable[1] : java.util.Arrays.copyOf(suppressed, suppressed.length + 1);
        s[s.length - 1] = t;
        suppressed = s;
    }
    public final synchronized Throwable[] getSuppressed() { return suppressed == null ? new Throwable[0] : suppressed.clone(); }

    public void printStackTrace() { printStackTrace(System.err); }
    public void printStackTrace(PrintStream s) { print(new Sink(s, null), "", ""); }
    public void printStackTrace(PrintWriter w) { print(new Sink(null, w), "", ""); }

    private static final class Sink {
        final PrintStream s;
        final PrintWriter w;
        Sink(PrintStream s, PrintWriter w) { this.s = s; this.w = w; }
        void line(String text) { if (s != null) s.println(text); else w.println(text); }
    }
    private void print(Sink out, String caption, String prefix) {
        out.line(prefix + caption + this);
        StackTraceElement[] trace = getOurStackTrace();
        for (StackTraceElement e : trace) out.line(prefix + "\tat " + e);
        if (suppressed != null) for (Throwable t : suppressed) t.print(out, "Suppressed: ", prefix + "\t");
        Throwable c = getCause();
        if (c != null && c != this) c.print(out, "Caused by: ", prefix);
    }
}
