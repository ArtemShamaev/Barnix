package java.lang;

public interface Appendable {
    Appendable append(CharSequence s) throws java.io.IOException;
    Appendable append(CharSequence s, int start, int end) throws java.io.IOException;
    Appendable append(char c) throws java.io.IOException;
}
