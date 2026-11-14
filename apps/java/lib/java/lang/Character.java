package java.lang;

public final class Character implements java.io.Serializable, Comparable<Character> {
    public static final char MIN_VALUE = '\u0000', MAX_VALUE = '￿';
    public static final int MIN_RADIX = 2, MAX_RADIX = 36, SIZE = 16, BYTES = 2;
    public static final char MIN_HIGH_SURROGATE = '\ud800', MAX_HIGH_SURROGATE = '\udbff', MIN_LOW_SURROGATE = '\udc00', MAX_LOW_SURROGATE = '\udfff';
    public static final char MIN_SURROGATE = '\ud800', MAX_SURROGATE = '\udfff';
    public static final int MIN_SUPPLEMENTARY_CODE_POINT = 0x10000, MAX_CODE_POINT = 0x10ffff, MIN_CODE_POINT = 0;
    @SuppressWarnings("unchecked")
    public static final Class<Character> TYPE = (Class<Character>) Class.getPrimitiveClass("char");
    private static final Character[] cache = new Character[128];
    private final char value;

    public Character(char c) { value = c; }
    public static Character valueOf(char c) {
        if (c < 128) { Character k = cache[c]; if (k == null) cache[c] = k = new Character(c); return k; }
        return new Character(c);
    }
    public char charValue() { return value; }
    public int hashCode() { return value; }
    public static int hashCode(char c) { return c; }
    public boolean equals(Object o) { return o instanceof Character && ((Character) o).value == value; }
    public int compareTo(Character o) { return value - o.value; }
    public static int compare(char a, char b) { return a - b; }
    public String toString() { return String.valueOf(value); }
    public static String toString(char c) { return String.valueOf(c); }

    public static boolean isDigit(char c) { return isDigit((int) c); }
    public static boolean isDigit(int c) { return c >= '0' && c <= '9' || c >= 0x660 && c <= 0x669 || c >= 0xff10 && c <= 0xff19; }
    public static boolean isLetter(char c) { return isLetter((int) c); }
    public static boolean isLetter(int c) {
        if (c < 128) return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z';
        if (c == 0xd7 || c == 0xf7) return false;
        return c >= 0xc0 && c <= 0x24f || c == 0xaa || c == 0xb5 || c == 0xba || c >= 0x370 && c <= 0x3ff && c != 0x37e && c != 0x387
            || c >= 0x400 && c <= 0x52f && !(c >= 0x482 && c <= 0x489) || c >= 0x531 && c <= 0x587 || c >= 0x5d0 && c <= 0x5ea
            || c >= 0x620 && c <= 0x64a || c >= 0x1e00 && c <= 0x1fff || c >= 0x3040 && c <= 0x30ff || c >= 0x4e00 && c <= 0x9fff
            || c >= 0xac00 && c <= 0xd7a3 || c >= 0xff21 && c <= 0xff3a || c >= 0xff41 && c <= 0xff5a;
    }
    public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
    public static boolean isLetterOrDigit(int c) { return isLetter(c) || isDigit(c); }
    public static boolean isAlphabetic(int c) { return isLetter(c); }
    public static boolean isJavaIdentifierStart(char c) { return isLetter(c) || c == '_' || c == '$'; }
    public static boolean isJavaIdentifierPart(char c) { return isLetterOrDigit(c) || c == '_' || c == '$'; }
    public static boolean isUpperCase(char c) { return isUpperCase((int) c); }
    public static boolean isUpperCase(int c) { return isLetter(c) && toLowerCase(c) != c; }
    public static boolean isLowerCase(char c) { return isLowerCase((int) c); }
    public static boolean isLowerCase(int c) { return isLetter(c) && toUpperCase(c) != c || c == 0xdf; }
    public static boolean isWhitespace(char c) { return isWhitespace((int) c); }
    public static boolean isWhitespace(int c) {
        return c == ' ' || c >= 9 && c <= 13 || c >= 0x1c && c <= 0x1f || c == 0x1680 || c >= 0x2000 && c <= 0x2006 || c >= 0x2008 && c <= 0x200a
            || c == 0x2028 || c == 0x2029 || c == 0x205f || c == 0x3000;
    }
    public static boolean isSpaceChar(char c) { return c == ' ' || c == 0xa0 || c >= 0x2000 && c <= 0x200a || c == 0x3000; }
    public static boolean isISOControl(char c) { return c <= 0x1f || c >= 0x7f && c <= 0x9f; }
    public static boolean isDefined(char c) { return true; }

    public static char toLowerCase(char c) { return (char) toLowerCase((int) c); }
    public static int toLowerCase(int c) {
        if (c < 128) return c >= 'A' && c <= 'Z' ? c + 32 : c;
        if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;
        if (c >= 0x100 && c <= 0x17f && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17f) {
            boolean odd = c >= 0x139 && c <= 0x148 || c >= 0x179 && c <= 0x17e;
            return odd ? ((c & 1) == 1 ? c + 1 : c) : ((c & 1) == 0 ? c + 1 : c);
        }
        if (c >= 0x391 && c <= 0x3ab && c != 0x3a2) return c + 32;
        if (c >= 0x410 && c <= 0x42f) return c + 32;
        if (c >= 0x400 && c <= 0x40f) return c + 80;
        if (c >= 0x460 && c <= 0x4ff && (c & 1) == 0 && !(c >= 0x482 && c <= 0x489) && c != 0x4c0) return c + 1;
        if (c >= 0xff21 && c <= 0xff3a) return c + 32;
        return c;
    }
    public static char toUpperCase(char c) { return (char) toUpperCase((int) c); }
    public static int toUpperCase(int c) {
        if (c < 128) return c >= 'a' && c <= 'z' ? c - 32 : c;
        if (c >= 0xe0 && c <= 0xfe && c != 0xf7) return c - 32;
        if (c == 0xff) return 0x178;
        if (c == 0xb5) return 0x39c;
        if (c >= 0x100 && c <= 0x17f && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17f) {
            boolean odd = c >= 0x139 && c <= 0x148 || c >= 0x179 && c <= 0x17e;
            return odd ? ((c & 1) == 0 ? c - 1 : c) : ((c & 1) == 1 ? c - 1 : c);
        }
        if (c >= 0x3b1 && c <= 0x3cb && c != 0x3c2) return c - 32;
        if (c >= 0x430 && c <= 0x44f) return c - 32;
        if (c >= 0x450 && c <= 0x45f) return c - 80;
        if (c >= 0x461 && c <= 0x4ff && (c & 1) == 1 && !(c >= 0x482 && c <= 0x489)) return c - 1;
        if (c >= 0xff41 && c <= 0xff5a) return c - 32;
        return c;
    }
    public static char toTitleCase(char c) { return toUpperCase(c); }

    public static int digit(char c, int radix) { return digit((int) c, radix); }
    public static int digit(int c, int radix) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 0xff10 && c <= 0xff19) d = c - 0xff10;
        else return -1;
        return d < radix && radix >= MIN_RADIX && radix <= MAX_RADIX ? d : -1;
    }
    public static int getNumericValue(char c) { return digit(c, 36); }
    public static char forDigit(int d, int radix) {
        if (d < 0 || d >= radix || radix < MIN_RADIX || radix > MAX_RADIX) return 0;
        return (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    public static boolean isHighSurrogate(char c) { return c >= MIN_HIGH_SURROGATE && c <= MAX_HIGH_SURROGATE; }
    public static boolean isLowSurrogate(char c) { return c >= MIN_LOW_SURROGATE && c <= MAX_LOW_SURROGATE; }
    public static boolean isSurrogate(char c) { return c >= MIN_SURROGATE && c <= MAX_SURROGATE; }
    public static boolean isSurrogatePair(char h, char l) { return isHighSurrogate(h) && isLowSurrogate(l); }
    public static boolean isSupplementaryCodePoint(int c) { return c >= 0x10000 && c <= MAX_CODE_POINT; }
    public static boolean isValidCodePoint(int c) { return c >= 0 && c <= MAX_CODE_POINT; }
    public static boolean isBmpCodePoint(int c) { return c >>> 16 == 0; }
    public static int charCount(int c) { return c >= 0x10000 ? 2 : 1; }
    public static int toCodePoint(char h, char l) { return ((h - 0xd800) << 10) + (l - 0xdc00) + 0x10000; }
    public static char[] toChars(int c) {
        if (c < 0x10000) return new char[] {(char) c};
        c -= 0x10000;
        return new char[] {(char) (0xd800 + (c >> 10)), (char) (0xdc00 + (c & 0x3ff))};
    }
    public static char highSurrogate(int c) { return (char) (0xd800 + ((c - 0x10000) >> 10)); }
    public static char lowSurrogate(int c) { return (char) (0xdc00 + ((c - 0x10000) & 0x3ff)); }
    public static char reverseBytes(char c) { return (char) ((c << 8) | (c >> 8)); }
    public static int getType(char c) {
        if (isUpperCase(c)) return 1;
        if (isLowerCase(c)) return 2;
        if (isDigit(c)) return 9;
        if (isLetter(c)) return 5;
        if (isSpaceChar(c)) return 12;
        if (isISOControl(c)) return 15;
        return 24;
    }
}
