package java.io;

public class StringReader extends Reader {
    private String s;
    private int pos, mark;
    public StringReader(String s) { this.s = s; }
    public int read() { return pos < s.length() ? s.charAt(pos++) : -1; }
    public int read(char[] c, int off, int len) {
        if (len == 0) return 0;
        if (pos >= s.length()) return -1;
        int n = Math.min(len, s.length() - pos);
        s.getChars(pos, pos + n, c, off);
        pos += n;
        return n;
    }
    public long skip(long n) { long k = Math.min(n, s.length() - pos); pos += k; return k; }
    public boolean ready() { return true; }
    public boolean markSupported() { return true; }
    public void mark(int limit) { mark = pos; }
    public void reset() { pos = mark; }
    public void close() {}
}
