package java.text;

public class ParsePosition {
    private int index, error = -1;
    public ParsePosition(int index) { this.index = index; }
    public int getIndex() { return index; }
    public void setIndex(int i) { index = i; }
    public int getErrorIndex() { return error; }
    public void setErrorIndex(int i) { error = i; }
}
