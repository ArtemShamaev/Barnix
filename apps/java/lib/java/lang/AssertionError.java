package java.lang;

public class AssertionError extends Error {
    public AssertionError() { super(); }
    public AssertionError(Object o) { super(String.valueOf(o)); if (o instanceof Throwable) initCause((Throwable) o); }
    public AssertionError(boolean b) { this(String.valueOf(b)); }
    public AssertionError(char c) { this(String.valueOf(c)); }
    public AssertionError(int i) { this(String.valueOf(i)); }
    public AssertionError(long l) { this(String.valueOf(l)); }
    public AssertionError(float f) { this(String.valueOf(f)); }
    public AssertionError(double d) { this(String.valueOf(d)); }
    public AssertionError(String s, Throwable cause) { super(s, cause); }
}
