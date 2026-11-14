package java.lang;

public final class StringBuffer extends AbstractStringBuilder implements java.io.Serializable, Comparable<StringBuffer> {
    public StringBuffer() { super(16); }
    public StringBuffer(int capacity) { super(capacity); }
    public StringBuffer(String s) { super(s.length() + 16); appendString(s); }
    public StringBuffer(CharSequence s) { super(s.length() + 16); appendSequence(s, 0, s.length()); }
    public synchronized StringBuffer append(Object o) { appendString(String.valueOf(o)); return this; }
    public synchronized StringBuffer append(String s) { appendString(s); return this; }
    public synchronized StringBuffer append(StringBuffer s) { appendString(s == null ? null : s.toString()); return this; }
    public synchronized StringBuffer append(CharSequence s) { if (s == null) appendString(null); else appendSequence(s, 0, s.length()); return this; }
    public synchronized StringBuffer append(CharSequence s, int start, int end) { appendSequence(s, start, end); return this; }
    public synchronized StringBuffer append(char[] c) { appendChars(c, 0, c.length); return this; }
    public synchronized StringBuffer append(char[] c, int off, int n) { appendChars(c, off, n); return this; }
    public synchronized StringBuffer append(boolean b) { appendString(b ? "true" : "false"); return this; }
    public synchronized StringBuffer append(char c) { appendChar(c); return this; }
    public synchronized StringBuffer append(int i) { appendInt(i); return this; }
    public synchronized StringBuffer append(long l) { appendLong(l); return this; }
    public synchronized StringBuffer append(float f) { appendString(Float.toString(f)); return this; }
    public synchronized StringBuffer append(double d) { appendString(Double.toString(d)); return this; }
    public synchronized StringBuffer appendCodePoint(int cp) { if (cp >= 0x10000) appendChars(Character.toChars(cp), 0, 2); else appendChar((char) cp); return this; }
    public synchronized StringBuffer delete(int start, int end) { deleteRange(start, end); return this; }
    public synchronized StringBuffer deleteCharAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); deleteRange(i, i + 1); return this; }
    public synchronized StringBuffer replace(int start, int end, String s) { replaceRange(start, end, s); return this; }
    public synchronized StringBuffer insert(int at, String s) { insertString(at, s); return this; }
    public synchronized StringBuffer insert(int at, Object o) { insertString(at, String.valueOf(o)); return this; }
    public synchronized StringBuffer insert(int at, char[] c) { insertString(at, new String(c)); return this; }
    public synchronized StringBuffer insert(int at, CharSequence s) { insertString(at, String.valueOf(s)); return this; }
    public synchronized StringBuffer insert(int at, boolean b) { insertString(at, String.valueOf(b)); return this; }
    public synchronized StringBuffer insert(int at, char c) { insertString(at, String.valueOf(c)); return this; }
    public synchronized StringBuffer insert(int at, int i) { insertString(at, String.valueOf(i)); return this; }
    public synchronized StringBuffer insert(int at, long l) { insertString(at, String.valueOf(l)); return this; }
    public synchronized StringBuffer insert(int at, float f) { insertString(at, String.valueOf(f)); return this; }
    public synchronized StringBuffer insert(int at, double d) { insertString(at, String.valueOf(d)); return this; }
    public synchronized StringBuffer reverse() { reverseChars(); return this; }
    public synchronized String toString() { return new String(value, 0, count); }
    public int compareTo(StringBuffer o) { return toString().compareTo(o.toString()); }
}
