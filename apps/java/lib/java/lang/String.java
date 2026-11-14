package java.lang;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Locale;

public final class String implements java.io.Serializable, Comparable<String>, CharSequence {
    final char[] value;
    private int hash;
    private static final char[] EMPTY = new char[0];

    public static final Comparator<String> CASE_INSENSITIVE_ORDER = new Comparator<String>() {
        public int compare(String a, String b) { return a.compareToIgnoreCase(b); }
    };

    public String() { value = EMPTY; }
    public String(String original) { value = original.value; hash = original.hash; }
    public String(char[] chars) { this(chars, 0, chars.length); }
    public String(char[] chars, int offset, int count) {
        if (offset < 0 || count < 0 || offset > chars.length - count) throw new StringIndexOutOfBoundsException("offset " + offset + ", count " + count + ", length " + chars.length);
        value = new char[count];
        System.arraycopy(chars, offset, value, 0, count);
    }
    String(char[] chars, boolean share) { value = chars; }
    public String(int[] codePoints, int offset, int count) {
        StringBuilder b = new StringBuilder(count);
        for (int i = 0; i < count; i++) b.appendCodePoint(codePoints[offset + i]);
        value = b.toString().value;
    }
    public String(byte[] bytes) { this(bytes, 0, bytes.length); }
    public String(byte[] bytes, int offset, int length) { value = decodeUtf8(bytes, offset, length); }
    public String(byte[] bytes, String charset) throws java.io.UnsupportedEncodingException { this(bytes, 0, bytes.length, charset); }
    public String(byte[] bytes, int offset, int length, String charset) throws java.io.UnsupportedEncodingException {
        String c = charset.toUpperCase();
        if (c.equals("UTF-8") || c.equals("UTF8")) value = decodeUtf8(bytes, offset, length);
        else if (c.equals("ISO-8859-1") || c.equals("US-ASCII") || c.equals("ASCII") || c.equals("LATIN1")) {
            value = new char[length];
            for (int i = 0; i < length; i++) value[i] = (char) (bytes[offset + i] & (c.startsWith("ISO") || c.equals("LATIN1") ? 0xff : 0x7f));
        } else if (c.equals("UTF-16BE") || c.equals("UTF-16")) {
            value = new char[length / 2];
            for (int i = 0; i < value.length; i++) value[i] = (char) ((bytes[offset + i * 2] & 0xff) << 8 | (bytes[offset + i * 2 + 1] & 0xff));
        } else if (c.equals("UTF-16LE")) {
            value = new char[length / 2];
            for (int i = 0; i < value.length; i++) value[i] = (char) ((bytes[offset + i * 2 + 1] & 0xff) << 8 | (bytes[offset + i * 2] & 0xff));
        } else throw new java.io.UnsupportedEncodingException(charset);
    }
    public String(StringBuilder b) { value = b.toString().value; }
    public String(StringBuffer b) { value = b.toString().value; }

    static char[] decodeUtf8(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || off > b.length - len) throw new StringIndexOutOfBoundsException();
        char[] out = new char[len];
        int n = 0, end = off + len;
        for (int i = off; i < end;) {
            int c = b[i++] & 0xff;
            if (c < 0x80) { out[n++] = (char) c; continue; }
            int need, cp;
            if (c >= 0xf0 && c < 0xf8) { need = 3; cp = c & 7; }
            else if (c >= 0xe0) { need = 2; cp = c & 15; }
            else if (c >= 0xc0) { need = 1; cp = c & 31; }
            else { out[n++] = '�'; continue; }
            if (i + need > end) { out[n++] = '�'; break; }
            boolean ok = true;
            for (int k = 0; k < need; k++) { int d = b[i + k] & 0xff; if ((d & 0xc0) != 0x80) { ok = false; break; } cp = cp << 6 | (d & 0x3f); }
            if (!ok) { out[n++] = '�'; continue; }
            i += need;
            if (cp >= 0x10000) {
                if (n + 2 > out.length) { char[] g = new char[out.length + 8]; System.arraycopy(out, 0, g, 0, n); out = g; }
                cp -= 0x10000;
                out[n++] = (char) (0xd800 + (cp >> 10));
                out[n++] = (char) (0xdc00 + (cp & 0x3ff));
            } else out[n++] = (char) cp;
        }
        if (n == out.length) return out;
        char[] r = new char[n];
        System.arraycopy(out, 0, r, 0, n);
        return r;
    }

    public int length() { return value.length; }
    public boolean isEmpty() { return value.length == 0; }
    public char charAt(int i) {
        if (i < 0 || i >= value.length) throw new StringIndexOutOfBoundsException("index " + i + ", length " + value.length);
        return value[i];
    }
    public int codePointAt(int i) {
        char c = charAt(i);
        if (Character.isHighSurrogate(c) && i + 1 < value.length && Character.isLowSurrogate(value[i + 1])) return Character.toCodePoint(c, value[i + 1]);
        return c;
    }
    public void getChars(int begin, int end, char[] dst, int at) {
        if (begin < 0 || end > value.length || begin > end) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, begin, dst, at, end - begin);
    }
    public char[] toCharArray() { char[] r = new char[value.length]; System.arraycopy(value, 0, r, 0, value.length); return r; }

    public byte[] getBytes() {
        int n = 0;
        for (int i = 0; i < value.length; i++) {
            char c = value[i];
            if (c < 0x80) n++;
            else if (c < 0x800) n += 2;
            else if (Character.isHighSurrogate(c) && i + 1 < value.length && Character.isLowSurrogate(value[i + 1])) { n += 4; i++; }
            else n += 3;
        }
        byte[] r = new byte[n];
        int k = 0;
        for (int i = 0; i < value.length; i++) {
            int c = value[i];
            if (c < 0x80) r[k++] = (byte) c;
            else if (c < 0x800) { r[k++] = (byte) (0xc0 | c >> 6); r[k++] = (byte) (0x80 | (c & 63)); }
            else if (Character.isHighSurrogate((char) c) && i + 1 < value.length && Character.isLowSurrogate(value[i + 1])) {
                int cp = Character.toCodePoint((char) c, value[++i]);
                r[k++] = (byte) (0xf0 | cp >> 18); r[k++] = (byte) (0x80 | (cp >> 12 & 63));
                r[k++] = (byte) (0x80 | (cp >> 6 & 63)); r[k++] = (byte) (0x80 | (cp & 63));
            } else { r[k++] = (byte) (0xe0 | c >> 12); r[k++] = (byte) (0x80 | (c >> 6 & 63)); r[k++] = (byte) (0x80 | (c & 63)); }
        }
        return r;
    }
    public byte[] getBytes(String charset) throws java.io.UnsupportedEncodingException {
        String c = charset.toUpperCase();
        if (c.equals("UTF-8") || c.equals("UTF8")) return getBytes();
        if (c.equals("ISO-8859-1") || c.equals("US-ASCII") || c.equals("ASCII") || c.equals("LATIN1")) {
            byte[] r = new byte[value.length];
            for (int i = 0; i < r.length; i++) r[i] = (byte) (value[i] < 256 ? value[i] : '?');
            return r;
        }
        if (c.equals("UTF-16BE") || c.equals("UTF-16")) {
            byte[] r = new byte[value.length * 2];
            for (int i = 0; i < value.length; i++) { r[i * 2] = (byte) (value[i] >> 8); r[i * 2 + 1] = (byte) value[i]; }
            return r;
        }
        throw new java.io.UnsupportedEncodingException(charset);
    }

    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof String)) return false;
        char[] a = value, b = ((String) o).value;
        if (a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public boolean contentEquals(CharSequence s) { return toString().equals(s.toString()); }
    public boolean equalsIgnoreCase(String o) {
        if (o == null || o.value.length != value.length) return false;
        return regionMatches(true, 0, o, 0, value.length);
    }
    public int hashCode() {
        int h = hash;
        if (h == 0 && value.length > 0) {
            char[] v = value;
            for (int i = 0; i < v.length; i++) h = 31 * h + v[i];
            hash = h;
        }
        return h;
    }
    public int compareTo(String o) {
        char[] a = value, b = o.value;
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) if (a[i] != b[i]) return a[i] - b[i];
        return a.length - b.length;
    }
    public int compareToIgnoreCase(String o) {
        int n = Math.min(value.length, o.value.length);
        for (int i = 0; i < n; i++) {
            char a = value[i], b = o.value[i];
            if (a != b) {
                a = Character.toLowerCase(Character.toUpperCase(a));
                b = Character.toLowerCase(Character.toUpperCase(b));
                if (a != b) return a - b;
            }
        }
        return value.length - o.value.length;
    }
    public boolean regionMatches(int to, String other, int oo, int len) { return regionMatches(false, to, other, oo, len); }
    public boolean regionMatches(boolean ignoreCase, int to, String other, int oo, int len) {
        if (to < 0 || oo < 0 || to > (long) value.length - len || oo > (long) other.value.length - len) return false;
        for (int i = 0; i < len; i++) {
            char a = value[to + i], b = other.value[oo + i];
            if (a == b) continue;
            if (!ignoreCase) return false;
            if (Character.toUpperCase(a) == Character.toUpperCase(b) || Character.toLowerCase(a) == Character.toLowerCase(b)) continue;
            return false;
        }
        return true;
    }
    public boolean startsWith(String p, int off) { return regionMatches(false, off, p, 0, p.value.length); }
    public boolean startsWith(String p) { return startsWith(p, 0); }
    public boolean endsWith(String s) { return startsWith(s, value.length - s.value.length); }

    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        if (ch >= 0x10000) return indexOf(new String(Character.toChars(ch), true), from);
        for (int i = from; i < value.length; i++) if (value[i] == ch) return i;
        return -1;
    }
    public int lastIndexOf(int ch) { return lastIndexOf(ch, value.length - 1); }
    public int lastIndexOf(int ch, int from) {
        for (int i = Math.min(from, value.length - 1); i >= 0; i--) if (value[i] == ch) return i;
        return -1;
    }
    public int indexOf(String s) { return indexOf(s, 0); }
    public int indexOf(String s, int from) {
        char[] v = value, w = s.value;
        if (from < 0) from = 0;
        if (w.length == 0) return from <= v.length ? from : v.length;
        outer:
        for (int i = from; i <= v.length - w.length; i++) {
            if (v[i] != w[0]) continue;
            for (int k = 1; k < w.length; k++) if (v[i + k] != w[k]) continue outer;
            return i;
        }
        return -1;
    }
    public int lastIndexOf(String s) { return lastIndexOf(s, value.length); }
    public int lastIndexOf(String s, int from) {
        int start = Math.min(from, value.length - s.value.length);
        outer:
        for (int i = start; i >= 0; i--) {
            for (int k = 0; k < s.value.length; k++) if (value[i + k] != s.value[k]) continue outer;
            return i;
        }
        return -1;
    }
    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }

    public String substring(int begin) { return substring(begin, value.length); }
    public String substring(int begin, int end) {
        if (begin < 0 || end > value.length || begin > end) throw new StringIndexOutOfBoundsException("begin " + begin + ", end " + end + ", length " + value.length);
        if (begin == 0 && end == value.length) return this;
        char[] r = new char[end - begin];
        System.arraycopy(value, begin, r, 0, r.length);
        return new String(r, true);
    }
    public CharSequence subSequence(int begin, int end) { return substring(begin, end); }
    public String concat(String s) {
        if (s.value.length == 0) return this;
        char[] r = new char[value.length + s.value.length];
        System.arraycopy(value, 0, r, 0, value.length);
        System.arraycopy(s.value, 0, r, value.length, s.value.length);
        return new String(r, true);
    }
    public String replace(char a, char b) {
        if (a == b || indexOf(a) < 0) return this;
        char[] r = toCharArray();
        for (int i = 0; i < r.length; i++) if (r[i] == a) r[i] = b;
        return new String(r, true);
    }
    public String replace(CharSequence target, CharSequence replacement) {
        String t = target.toString(), r = replacement.toString();
        if (t.length() == 0) {
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < value.length; i++) b.append(r).append(value[i]);
            return b.append(r).toString();
        }
        int i = indexOf(t);
        if (i < 0) return this;
        StringBuilder b = new StringBuilder();
        int last = 0;
        while (i >= 0) { b.append(value, last, i - last).append(r); last = i + t.length(); i = indexOf(t, last); }
        return b.append(value, last, value.length - last).toString();
    }
    public String toLowerCase() {
        for (int i = 0; i < value.length; i++)
            if (Character.toLowerCase(value[i]) != value[i]) {
                char[] r = toCharArray();
                for (int k = i; k < r.length; k++) r[k] = Character.toLowerCase(r[k]);
                return new String(r, true);
            }
        return this;
    }
    public String toUpperCase() {
        for (int i = 0; i < value.length; i++)
            if (Character.toUpperCase(value[i]) != value[i]) {
                char[] r = toCharArray();
                for (int k = i; k < r.length; k++) r[k] = Character.toUpperCase(r[k]);
                return new String(r, true);
            }
        return this;
    }
    public String toLowerCase(Locale l) { return toLowerCase(); }
    public String toUpperCase(Locale l) { return toUpperCase(); }
    public String trim() {
        int a = 0, b = value.length;
        while (a < b && value[a] <= ' ') a++;
        while (b > a && value[b - 1] <= ' ') b--;
        return substring(a, b);
    }
    public String strip() { return trim(); }
    public boolean isBlank() { return trim().isEmpty(); }
    public String repeat(int n) { StringBuilder b = new StringBuilder(); for (int i = 0; i < n; i++) b.append(this); return b.toString(); }
    public String toString() { return this; }
    public native String intern();

    public boolean matches(String regex) { return java.util.regex.Pattern.matches(regex, this); }
    public String replaceAll(String regex, String replacement) { return java.util.regex.Pattern.compile(regex).matcher(this).replaceAll(replacement); }
    public String replaceFirst(String regex, String replacement) { return java.util.regex.Pattern.compile(regex).matcher(this).replaceFirst(replacement); }
    public String[] split(String regex) { return split(regex, 0); }
    public String[] split(String regex, int limit) { return java.util.regex.Pattern.compile(regex).split(this, limit); }

    public static String valueOf(Object o) { return o == null ? "null" : o.toString(); }
    public static String valueOf(char[] c) { return new String(c); }
    public static String valueOf(char[] c, int off, int n) { return new String(c, off, n); }
    public static String copyValueOf(char[] c) { return new String(c); }
    public static String copyValueOf(char[] c, int off, int n) { return new String(c, off, n); }
    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { return new String(new char[] {c}, true); }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }
    public static String format(String format, Object... args) { return new java.util.Formatter().format(format, args).toString(); }
    public static String format(Locale l, String format, Object... args) { return format(format, args); }
    public static String join(CharSequence sep, CharSequence... parts) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < parts.length; i++) { if (i > 0) b.append(sep); b.append(parts[i]); }
        return b.toString();
    }
    public static String join(CharSequence sep, Iterable<? extends CharSequence> parts) {
        StringBuilder b = new StringBuilder();
        boolean first = true;
        for (CharSequence p : parts) { if (!first) b.append(sep); b.append(p); first = false; }
        return b.toString();
    }
}
