package java.io;

public class IOException extends Exception {
    public IOException() {}
    public IOException(String s) { super(s); }
    public IOException(String s, Throwable cause) { super(s, cause); }
    public IOException(Throwable cause) { super(cause); }
}
