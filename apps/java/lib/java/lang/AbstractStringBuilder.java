package java.lang;

abstract class AbstractStringBuilder implements Appendable, CharSequence {
    char[] value;
    int count;

    AbstractStringBuilder(int capacity) { value = new char[capacity]; }
    public int length() { return count; }
    public int capacity() { return value.length; }
    public void ensureCapacity(int n) { if (n > value.length) grow(n); }
    private void grow(int need) {
        int n = value.length * 2 + 2;
        if (n < need) n = need;
        char[] v = new char[n];
        System.arraycopy(value, 0, v, 0, count);
        value = v;
    }
    public void trimToSize() { if (count < value.length) { char[] v = new char[count]; System.arraycopy(value, 0, v, 0, count); value = v; } }
    public void setLength(int n) {
        if (n < 0) throw new StringIndexOutOfBoundsException(n);
        ensureCapacity(n);
        for (int i = count; i < n; i++) value[i] = 0;
        count = n;
    }
    public char charAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); return value[i]; }
    public void setCharAt(int i, char c) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); value[i] = c; }
    public void getChars(int begin, int end, char[] dst, int at) { System.arraycopy(value, begin, dst, at, end - begin); }

    AbstractStringBuilder appendChars(char[] c, int off, int n) {
        if (count + n > value.length) grow(count + n);
        System.arraycopy(c, off, value, count, n);
        count += n;
        return this;
    }
    AbstractStringBuilder appendString(String s) {
        if (s == null) s = "null";
        return appendChars(s.value, 0, s.value.length);
    }
    AbstractStringBuilder appendChar(char c) {
        if (count == value.length) grow(count + 1);
        value[count++] = c;
        return this;
    }
    AbstractStringBuilder appendInt(int i) {
        if (i == Integer.MIN_VALUE) return appendString("-2147483648");
        if (count + 11 > value.length) grow(count + 11);
        if (i < 0) { value[count++] = '-'; i = -i; }
        int start = count;
        do { value[count++] = (char) ('0' + i % 10); i /= 10; } while (i != 0);
        for (int a = start, b = count - 1; a < b; a++, b--) { char t = value[a]; value[a] = value[b]; value[b] = t; }
        return this;
    }
    AbstractStringBuilder appendLong(long l) {
        if (l >= Integer.MIN_VALUE && l <= Integer.MAX_VALUE) return appendInt((int) l);
        return appendString(Long.toString(l));
    }
    AbstractStringBuilder appendSequence(CharSequence s, int start, int end) {
        if (s == null) return appendString("null");
        if (s instanceof String) { String t = (String) s; return appendChars(t.value, start, end - start); }
        for (int i = start; i < end; i++) appendChar(s.charAt(i));
        return this;
    }
    AbstractStringBuilder insertString(int at, String s) {
        if (at < 0 || at > count) throw new StringIndexOutOfBoundsException(at);
        if (s == null) s = "null";
        int n = s.value.length;
        if (count + n > value.length) grow(count + n);
        System.arraycopy(value, at, value, at + n, count - at);
        System.arraycopy(s.value, 0, value, at, n);
        count += n;
        return this;
    }
    AbstractStringBuilder deleteRange(int start, int end) {
        if (end > count) end = count;
        if (start < 0 || start > end) throw new StringIndexOutOfBoundsException(start);
        System.arraycopy(value, end, value, start, count - end);
        count -= end - start;
        return this;
    }
    AbstractStringBuilder replaceRange(int start, int end, String s) {
        if (end > count) end = count;
        deleteRange(start, end);
        return insertString(start, s);
    }
    AbstractStringBuilder reverseChars() {
        for (int a = 0, b = count - 1; a < b; a++, b--) { char t = value[a]; value[a] = value[b]; value[b] = t; }
        for (int i = 0; i < count - 1; i++)
            if (Character.isLowSurrogate(value[i]) && Character.isHighSurrogate(value[i + 1])) { char t = value[i]; value[i] = value[i + 1]; value[i + 1] = t; i++; }
        return this;
    }
    public int indexOf(String s) { return toString().indexOf(s); }
    public int indexOf(String s, int from) { return toString().indexOf(s, from); }
    public int lastIndexOf(String s) { return toString().lastIndexOf(s); }
    public int lastIndexOf(String s, int from) { return toString().lastIndexOf(s, from); }
    public String substring(int start) { return substring(start, count); }
    public String substring(int start, int end) {
        if (start < 0 || end > count || start > end) throw new StringIndexOutOfBoundsException(start);
        return new String(value, start, end - start);
    }
    public CharSequence subSequence(int start, int end) { return substring(start, end); }
    public String toString() { return new String(value, 0, count); }
}
