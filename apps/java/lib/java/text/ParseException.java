package java.text;

public class ParseException extends Exception {
    private final int offset;
    public ParseException(String s, int offset) { super(s); this.offset = offset; }
    public int getErrorOffset() { return offset; }
}
