package java.io;

public class UncheckedIOException extends RuntimeException {
    public UncheckedIOException(String s, IOException cause) { super(s, cause); }
    public UncheckedIOException(IOException cause) { super(cause); }
}
