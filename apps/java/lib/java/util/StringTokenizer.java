package java.util;

public class StringTokenizer implements Enumeration<Object> {
    private final String str;
    private String delims;
    private final boolean returnDelims;
    private int pos;

    public StringTokenizer(String s, String delims, boolean returnDelims) { str = s; this.delims = delims; this.returnDelims = returnDelims; }
    public StringTokenizer(String s, String delims) { this(s, delims, false); }
    public StringTokenizer(String s) { this(s, " \t\n\r\f", false); }
    private int skip(int p) { if (!returnDelims) while (p < str.length() && delims.indexOf(str.charAt(p)) >= 0) p++; return p; }
    public boolean hasMoreTokens() { return skip(pos) < str.length(); }
    public String nextToken() {
        pos = skip(pos);
        if (pos >= str.length()) throw new NoSuchElementException();
        int start = pos;
        if (returnDelims && delims.indexOf(str.charAt(pos)) >= 0) return str.substring(start, ++pos);
        while (pos < str.length() && delims.indexOf(str.charAt(pos)) < 0) pos++;
        return str.substring(start, pos);
    }
    public String nextToken(String d) { delims = d; return nextToken(); }
    public boolean hasMoreElements() { return hasMoreTokens(); }
    public Object nextElement() { return nextToken(); }
    public int countTokens() { int n = 0, p = pos; StringTokenizer t = new StringTokenizer(str.substring(p), delims, returnDelims); while (t.hasMoreTokens()) { t.nextToken(); n++; } return n; }
}
