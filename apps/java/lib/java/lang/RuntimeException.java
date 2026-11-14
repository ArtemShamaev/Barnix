package java.lang;

public class RuntimeException extends Exception {
    public RuntimeException() { super(); }
    public RuntimeException(String s) { super(s); }
    public RuntimeException(String s, Throwable cause) { super(s, cause); }
    public RuntimeException(Throwable cause) { super(cause); }
    protected RuntimeException(String s, Throwable cause, boolean suppression, boolean writable) { super(s, cause, suppression, writable); }
}
