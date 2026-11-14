package java.nio.charset;

public class UnsupportedCharsetException extends IllegalArgumentException {
    private final String name;
    public UnsupportedCharsetException(String name) { super(name); this.name = name; }
    public String getCharsetName() { return name; }
}
