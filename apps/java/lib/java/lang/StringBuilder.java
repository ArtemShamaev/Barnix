package java.lang;

public final class StringBuilder extends AbstractStringBuilder implements java.io.Serializable, Comparable<StringBuilder> {
    public StringBuilder() { super(16); }
    public StringBuilder(int capacity) { super(capacity); }
    public StringBuilder(String s) { super(s.length() + 16); appendString(s); }
    public StringBuilder(CharSequence s) { super(s.length() + 16); appendSequence(s, 0, s.length()); }
    public StringBuilder append(Object o) { appendString(String.valueOf(o)); return this; }
    public StringBuilder append(String s) { appendString(s); return this; }
    public StringBuilder append(StringBuffer s) { appendString(s == null ? null : s.toString()); return this; }
    public StringBuilder append(CharSequence s) { if (s == null) appendString(null); else appendSequence(s, 0, s.length()); return this; }
    public StringBuilder append(CharSequence s, int start, int end) { appendSequence(s, start, end); return this; }
    public StringBuilder append(char[] c) { appendChars(c, 0, c.length); return this; }
    public StringBuilder append(char[] c, int off, int n) { appendChars(c, off, n); return this; }
    public StringBuilder append(boolean b) { appendString(b ? "true" : "false"); return this; }
    public StringBuilder append(char c) { appendChar(c); return this; }
    public StringBuilder append(int i) { appendInt(i); return this; }
    public StringBuilder append(long l) { appendLong(l); return this; }
    public StringBuilder append(float f) { appendString(Float.toString(f)); return this; }
    public StringBuilder append(double d) { appendString(Double.toString(d)); return this; }
    public StringBuilder appendCodePoint(int cp) { if (cp >= 0x10000) appendChars(Character.toChars(cp), 0, 2); else appendChar((char) cp); return this; }
    public StringBuilder delete(int start, int end) { deleteRange(start, end); return this; }
    public StringBuilder deleteCharAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); deleteRange(i, i + 1); return this; }
    public StringBuilder replace(int start, int end, String s) { replaceRange(start, end, s); return this; }
    public StringBuilder insert(int at, String s) { insertString(at, s); return this; }
    public StringBuilder insert(int at, Object o) { insertString(at, String.valueOf(o)); return this; }
    public StringBuilder insert(int at, char[] c) { insertString(at, new String(c)); return this; }
    public StringBuilder insert(int at, CharSequence s) { insertString(at, String.valueOf(s)); return this; }
    public StringBuilder insert(int at, boolean b) { insertString(at, String.valueOf(b)); return this; }
    public StringBuilder insert(int at, char c) { insertString(at, String.valueOf(c)); return this; }
    public StringBuilder insert(int at, int i) { insertString(at, String.valueOf(i)); return this; }
    public StringBuilder insert(int at, long l) { insertString(at, String.valueOf(l)); return this; }
    public StringBuilder insert(int at, float f) { insertString(at, String.valueOf(f)); return this; }
    public StringBuilder insert(int at, double d) { insertString(at, String.valueOf(d)); return this; }
    public StringBuilder reverse() { reverseChars(); return this; }
    public String toString() { return new String(value, 0, count); }
    public int compareTo(StringBuilder o) { return toString().compareTo(o.toString()); }
}
