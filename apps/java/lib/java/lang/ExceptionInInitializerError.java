package java.lang;

public class ExceptionInInitializerError extends LinkageError {
    public ExceptionInInitializerError() { super(); }
    public ExceptionInInitializerError(String s) { super(s); }
    public ExceptionInInitializerError(Throwable thrown) { super(null, thrown); }
    public Throwable getException() { return getCause(); }
}
