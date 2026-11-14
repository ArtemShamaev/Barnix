package java.awt;

public class HeadlessException extends UnsupportedOperationException {
    public HeadlessException() {}
    public HeadlessException(String s) { super(s); }
}
