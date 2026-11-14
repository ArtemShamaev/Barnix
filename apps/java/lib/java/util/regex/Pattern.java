package java.util.regex;

import java.util.ArrayList;

/* A backtracking regular expression engine: literals, ., classes with
 * ranges and \d\w\s escapes, anchors ^ $ \b, groups (capturing, (?:),
 * named (?<n>) as numbered), alternation, greedy/lazy/possessive-as-greedy
 * quantifiers * + ? {n,m}, back references, (?i) and CASE_INSENSITIVE. */
public final class Pattern implements java.io.Serializable {
    public static final int UNIX_LINES = 1, CASE_INSENSITIVE = 2, COMMENTS = 4, MULTILINE = 8, LITERAL = 16, DOTALL = 32, UNICODE_CASE = 64;
    static final int CHAR = 0, ANY = 1, CLASS = 2, SPLIT = 3, JMP = 4, SAVE = 5, MATCH = 6, BOL = 7, EOL = 8, WORDB = 9, NWORDB = 10, BACKREF = 11, STRBEGIN = 12, STREND = 13;

    private final String source;
    private final int flags;
    int[] op = new int[16], a = new int[16], b = new int[16];
    Object[] cls = new Object[16];
    int count, groups;
    private int pos;
    private boolean ignoreCase, dotAll, multiline;
    char literalChar = 0xffff;

    private Pattern(String regex, int flags) {
        source = regex;
        this.flags = flags;
        ignoreCase = (flags & CASE_INSENSITIVE) != 0;
        dotAll = (flags & DOTALL) != 0;
        multiline = (flags & MULTILINE) != 0;
        if ((flags & LITERAL) != 0) {
            emit(SAVE, 0, 0);
            for (int i = 0; i < regex.length(); i++) emit(CHAR, regex.charAt(i), 0);
            emit(SAVE, 1, 0);
            emit(MATCH, 0, 0);
            if (regex.length() == 1) literalChar = regex.charAt(0);
            return;
        }
        if (regex.length() == 1 && ".$|()[{^?*+\\".indexOf(regex.charAt(0)) < 0) literalChar = regex.charAt(0);
        if (regex.length() == 2 && regex.charAt(0) == '\\' && !Character.isLetterOrDigit(regex.charAt(1))) literalChar = regex.charAt(1);
        emit(SAVE, 0, 0);
        alternation();
        if (pos < source.length()) throw error("Unmatched closing ')'");
        emit(SAVE, 1, 0);
        emit(MATCH, 0, 0);
    }
    public static Pattern compile(String regex) { return new Pattern(regex, 0); }
    public static Pattern compile(String regex, int flags) { return new Pattern(regex, flags); }
    public static boolean matches(String regex, CharSequence input) { return compile(regex).matcher(input).matches(); }
    public static String quote(String s) { return "\\Q" + s + "\\E"; }
    public String pattern() { return source; }
    public int flags() { return flags; }
    public String toString() { return source; }
    public Matcher matcher(CharSequence input) { return new Matcher(this, input); }
    public String[] split(CharSequence input) { return split(input, 0); }
    public String[] split(CharSequence input, int limit) {
        String s = input.toString();
        ArrayList<String> parts = new ArrayList<String>();
        int last = 0;
        if (literalChar != 0xffff) {
            for (int i = 0; i < s.length(); i++)
                if (s.charAt(i) == literalChar && (limit <= 0 || parts.size() < limit - 1)) { parts.add(s.substring(last, i)); last = i + 1; }
        } else {
            Matcher m = matcher(s);
            while (m.find() && (limit <= 0 || parts.size() < limit - 1)) {
                if (m.end() == 0) continue;
                parts.add(s.substring(last, m.start()));
                last = m.end();
            }
        }
        if (last == 0 && parts.isEmpty()) return new String[] {s};
        parts.add(s.substring(last));
        if (limit == 0) while (!parts.isEmpty() && parts.get(parts.size() - 1).length() == 0) parts.remove(parts.size() - 1);
        return parts.toArray(new String[parts.size()]);
    }

    private PatternSyntaxException error(String message) { return new PatternSyntaxException(message, source, pos); }
    private int emit(int o, int x, int y) {
        if (count == op.length) {
            op = java.util.Arrays.copyOf(op, count * 2); a = java.util.Arrays.copyOf(a, count * 2);
            b = java.util.Arrays.copyOf(b, count * 2); cls = java.util.Arrays.copyOf(cls, count * 2);
        }
        op[count] = o; a[count] = x; b[count] = y; cls[count] = null;
        return count++;
    }
    private void insert(int at, int o, int x, int y) {
        emit(0, 0, 0);
        System.arraycopy(op, at, op, at + 1, count - 1 - at);
        System.arraycopy(a, at, a, at + 1, count - 1 - at);
        System.arraycopy(b, at, b, at + 1, count - 1 - at);
        System.arraycopy(cls, at, cls, at + 1, count - 1 - at);
        for (int i = 0; i < count; i++) {
            if (i == at) continue;
            if (op[i] == SPLIT || op[i] == JMP) { if (a[i] > at || (a[i] == at && i > at)) a[i]++; if (op[i] == SPLIT && (b[i] > at || (b[i] == at && i > at))) b[i]++; }
        }
        op[at] = o; a[at] = x; b[at] = y; cls[at] = null;
    }
    private int[][] copyRange(int from, int to) {
        int n = to - from;
        int[][] r = new int[3][n];
        System.arraycopy(op, from, r[0], 0, n); System.arraycopy(a, from, r[1], 0, n); System.arraycopy(b, from, r[2], 0, n);
        return r;
    }
    private void paste(int[][] code, Object[] classes, int from) {
        int base = count;
        for (int i = 0; i < code[0].length; i++) {
            int o = code[0][i], x = code[1][i], y = code[2][i];
            if (o == SPLIT || o == JMP) { x += base - from; if (o == SPLIT) y += base - from; }
            int k = emit(o, x, y);
            cls[k] = classes[i];
        }
    }

    private boolean more() { return pos < source.length(); }
    private char peek() { return source.charAt(pos); }
    private void alternation() {
        int start = count;
        concatenation();
        while (more() && peek() == '|') {
            pos++;
            insert(start, SPLIT, start + 1, 0);
            int jmp = emit(JMP, 0, 0);
            b[start] = count;
            concatenation();
            a[jmp] = count;
        }
    }
    private void concatenation() {
        while (more() && peek() != '|' && peek() != ')') repetition();
    }
    private void repetition() {
        int start = count;
        atom();
        if (!more()) return;
        char c = peek();
        int min, max;
        if (c == '*') { min = 0; max = -1; pos++; }
        else if (c == '+') { min = 1; max = -1; pos++; }
        else if (c == '?') { min = 0; max = 1; pos++; }
        else if (c == '{' && pos + 1 < source.length() && Character.isDigit(source.charAt(pos + 1))) {
            int close = source.indexOf('}', pos);
            if (close < 0) throw error("Unclosed counted closure");
            String body = source.substring(pos + 1, close);
            int comma = body.indexOf(',');
            min = Integer.parseInt(comma < 0 ? body : body.substring(0, comma).trim());
            max = comma < 0 ? min : body.substring(comma + 1).trim().length() == 0 ? -1 : Integer.parseInt(body.substring(comma + 1).trim());
            pos = close + 1;
        } else return;
        boolean lazy = false;
        if (more() && peek() == '?') { lazy = true; pos++; }
        else if (more() && peek() == '+') pos++;
        int end = count;
        int[][] body = copyRange(start, end);
        Object[] bodyClasses = new Object[end - start];
        System.arraycopy(cls, start, bodyClasses, 0, end - start);
        count = start;
        for (int i = 0; i < min; i++) paste(body, bodyClasses, start);
        if (max < 0) {
            int split = emit(SPLIT, 0, 0);
            paste(body, bodyClasses, start);
            emit(JMP, split, 0);
            if (lazy) { a[split] = count; b[split] = split + 1; } else { a[split] = split + 1; b[split] = count; }
        } else {
            int[] splits = new int[max - min];
            for (int i = min; i < max; i++) {
                splits[i - min] = emit(SPLIT, 0, 0);
                paste(body, bodyClasses, start);
            }
            for (int s : splits) if (lazy) { a[s] = count; b[s] = s + 1; } else { a[s] = s + 1; b[s] = count; }
        }
    }
    private void atom() {
        char c = source.charAt(pos++);
        switch (c) {
        case '.': emit(ANY, dotAll ? 1 : 0, 0); return;
        case '^': emit(multiline ? BOL : STRBEGIN, 0, 0); return;
        case '$': emit(multiline ? EOL : STREND, 0, 0); return;
        case '[': { int k = emit(CLASS, 0, 0); cls[k] = parseClass(); return; }
        case '(': {
            int group = -1;
            if (more() && peek() == '?') {
                pos++;
                char k = source.charAt(pos++);
                if (k == '<' && more() && peek() != '=' && peek() != '!') {
                    int close = source.indexOf('>', pos);
                    pos = close + 1;
                    group = ++groups;
                } else if (k == ':') group = -1;
                else if (k == 'i' || k == '-') {
                    while (more() && peek() != ')' && peek() != ':') pos++;
                    if (k == 'i') ignoreCase = true;
                    if (more() && peek() == ')') { pos++; return; }
                    pos++;
                } else throw error("Unsupported group construct");
            } else group = ++groups;
            if (group > 0) emit(SAVE, group * 2, 0);
            alternation();
            if (!more() || peek() != ')') throw error("Unclosed group");
            pos++;
            if (group > 0) emit(SAVE, group * 2 + 1, 0);
            return;
        }
        case '\\': escape(); return;
        default: literal(c);
        }
    }
    private void literal(char c) {
        if (ignoreCase && Character.toLowerCase(c) != Character.toUpperCase(c)) {
            int k = emit(CLASS, 0, 0);
            cls[k] = new char[] {0, Character.toLowerCase(c), Character.toLowerCase(c), Character.toUpperCase(c), Character.toUpperCase(c)};
        } else emit(CHAR, c, 0);
    }
    private void escape() {
        char c = source.charAt(pos++);
        switch (c) {
        case 'd': case 'D': case 'w': case 'W': case 's': case 'S': { int k = emit(CLASS, 0, 0); cls[k] = shorthand(c); return; }
        case 'b': emit(WORDB, 0, 0); return;
        case 'B': emit(NWORDB, 0, 0); return;
        case 'A': emit(STRBEGIN, 0, 0); return;
        case 'z': case 'Z': emit(STREND, 0, 0); return;
        case 'Q': {
            int end = source.indexOf("\\E", pos);
            if (end < 0) end = source.length();
            for (int i = pos; i < end; i++) literal(source.charAt(i));
            pos = Math.min(source.length(), end + 2);
            return;
        }
        default:
            if (c >= '1' && c <= '9') { emit(BACKREF, c - '0', 0); return; }
            literal(escapedChar(c));
        }
    }
    private char escapedChar(char c) {
        switch (c) {
        case 't': return '\t';
        case 'n': return '\n';
        case 'r': return '\r';
        case 'f': return '\f';
        case 'e': return 27;
        case '0': { int v = 0; while (more() && peek() >= '0' && peek() <= '7') v = v * 8 + source.charAt(pos++) - '0'; return (char) v; }
        case 'x': { int v = Integer.parseInt(source.substring(pos, pos + 2), 16); pos += 2; return (char) v; }
        case 'u': { int v = Integer.parseInt(source.substring(pos, pos + 4), 16); pos += 4; return (char) v; }
        default: return c;
        }
    }
    /* Class encoding: char[0] = negated (0/1), then (lo, hi) pairs; nested classes for \w etc. are flattened. */
    private static char[] shorthand(char c) {
        String r;
        switch (Character.toLowerCase(c)) {
        case 'd': r = "09"; break;
        case 'w': r = "azAZ09__"; break;
        default: r = "  \t\r\u000b\u000b\f\f";
        }
        char[] k = new char[r.length() + 1];
        k[0] = (char) (Character.isUpperCase(c) ? 1 : 0);
        r.getChars(0, r.length(), k, 1);
        if (Character.toLowerCase(c) == 's') { k[3] = '\t'; k[4] = '\r'; }
        return k;
    }
    private char[] parseClass() {
        StringBuilder ranges = new StringBuilder();
        boolean negate = false;
        if (more() && peek() == '^') { negate = true; pos++; }
        boolean first = true;
        while (more() && (peek() != ']' || first)) {
            first = false;
            char c = source.charAt(pos++);
            if (c == '[') { char[] inner = parseClass(); if (inner[0] == 0) ranges.append(inner, 1, inner.length - 1); continue; }
            if (c == '&' && more() && peek() == '&') { pos++; continue; }
            if (c == '\\') {
                char e = source.charAt(pos++);
                if ("dDwWsS".indexOf(e) >= 0) {
                    char[] s = shorthand(e);
                    if (s[0] == 0) ranges.append(s, 1, s.length - 1);
                    else { /* negated shorthand inside a class: approximate by its complement ranges */
                        char last = 0;
                        java.util.ArrayList<char[]> pairs = new java.util.ArrayList<char[]>();
                        for (int i = 1; i + 1 < s.length; i += 2) pairs.add(new char[] {s[i], s[i + 1]});
                        java.util.Collections.sort(pairs, new java.util.Comparator<char[]>() { public int compare(char[] x, char[] y) { return x[0] - y[0]; } });
                        for (char[] p : pairs) { if (p[0] > last) ranges.append(last).append((char) (p[0] - 1)); last = (char) (p[1] + 1); }
                        ranges.append(last).append('￿');
                    }
                    continue;
                }
                c = escapedChar(e);
            }
            char hi = c;
            if (pos + 1 < source.length() && peek() == '-' && source.charAt(pos + 1) != ']') {
                pos++;
                hi = source.charAt(pos++);
                if (hi == '\\') hi = escapedChar(source.charAt(pos++));
            }
            ranges.append(c).append(hi);
            if (ignoreCase) {
                for (char x = c; x <= hi && x != 0xffff; x++) {
                    char l = Character.toLowerCase(x), u = Character.toUpperCase(x);
                    if (l != x) ranges.append(l).append(l);
                    if (u != x) ranges.append(u).append(u);
                    if (hi - c > 256) break;
                }
            }
        }
        if (!more()) throw error("Unclosed character class");
        pos++;
        char[] k = new char[ranges.length() + 1];
        k[0] = (char) (negate ? 1 : 0);
        ranges.getChars(0, ranges.length(), k, 1);
        return k;
    }
    static boolean inClass(char[] k, char c) {
        boolean in = false;
        for (int i = 1; i + 1 < k.length; i += 2) if (c >= k[i] && c <= k[i + 1]) { in = true; break; }
        return in != (k[0] == 1);
    }
}
