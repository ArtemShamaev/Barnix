package java.util.regex;

public final class Matcher implements MatchResult {
    private Pattern pattern;
    private CharSequence text;
    private int[] groups;
    private int from, appendPos, searchFrom;
    private boolean matched;
    private int[] stack = new int[64];

    Matcher(Pattern p, CharSequence text) { pattern = p; this.text = text; groups = new int[(p.groups + 1) * 2]; reset(); }
    public Pattern pattern() { return pattern; }
    public Matcher reset() { matched = false; appendPos = 0; searchFrom = 0; java.util.Arrays.fill(groups, -1); return this; }
    public Matcher reset(CharSequence input) { text = input; return reset(); }
    public Matcher usePattern(Pattern p) { pattern = p; groups = new int[(p.groups + 1) * 2]; java.util.Arrays.fill(groups, -1); return this; }

    private boolean word(int i) { return i >= 0 && i < text.length() && (Character.isLetterOrDigit(text.charAt(i)) || text.charAt(i) == '_'); }
    private int top;
    private void push(int x, int y) {
        if (top + 2 > stack.length) stack = java.util.Arrays.copyOf(stack, stack.length * 2);
        stack[top++] = x; stack[top++] = y;
    }
    /* Backtracking: stack entries are (pc, position) branches or (-1 - slot, old value) undo records. */
    private boolean run(int start, boolean anchoredEnd) {
        Pattern p = pattern;
        int[] op = p.op, a = p.a, b = p.b;
        Object[] cls = p.cls;
        int n = text.length();
        top = 0;
        java.util.Arrays.fill(groups, -1);
        int pc = 0, i = start;
        for (;;) {
            boolean fail = false;
            switch (op[pc]) {
            case Pattern.CHAR: if (i < n && text.charAt(i) == a[pc]) { i++; pc++; } else fail = true; break;
            case Pattern.ANY: if (i < n && (a[pc] == 1 || text.charAt(i) != '\n')) { i++; pc++; } else fail = true; break;
            case Pattern.CLASS: if (i < n && Pattern.inClass((char[]) cls[pc], text.charAt(i))) { i++; pc++; } else fail = true; break;
            case Pattern.SPLIT: push(b[pc], i); pc = a[pc]; break;
            case Pattern.JMP: pc = a[pc]; break;
            case Pattern.SAVE: push(-1 - a[pc], groups[a[pc]]); groups[a[pc]] = i; pc++; break;
            case Pattern.BOL: if (i == 0 || text.charAt(i - 1) == '\n') pc++; else fail = true; break;
            case Pattern.EOL: if (i == n || text.charAt(i) == '\n') pc++; else fail = true; break;
            case Pattern.STRBEGIN: if (i == 0) pc++; else fail = true; break;
            case Pattern.STREND: if (i == n || (i == n - 1 && text.charAt(i) == '\n')) pc++; else fail = true; break;
            case Pattern.WORDB: if (word(i - 1) != word(i)) pc++; else fail = true; break;
            case Pattern.NWORDB: if (word(i - 1) == word(i)) pc++; else fail = true; break;
            case Pattern.BACKREF: {
                int g = a[pc], s = g * 2 < groups.length ? groups[g * 2] : -1, e = g * 2 + 1 < groups.length ? groups[g * 2 + 1] : -1;
                if (s < 0 || e < 0) { fail = true; break; }
                int len = e - s;
                if (i + len > n) { fail = true; break; }
                for (int k = 0; k < len && !fail; k++) if (text.charAt(s + k) != text.charAt(i + k)) fail = true;
                if (!fail) { i += len; pc++; }
                break;
            }
            case Pattern.MATCH:
                if (!anchoredEnd || i == n) return true;
                fail = true;
                break;
            }
            if (!fail) continue;
            for (;;) {
                if (top == 0) return false;
                int y = stack[--top], x = stack[--top];
                if (x < 0) { groups[-1 - x] = y; continue; }
                pc = x; i = y;
                break;
            }
        }
    }
    public boolean matches() { matched = run(0, true); if (matched) searchFrom = groups[1]; return matched; }
    public boolean lookingAt() { matched = run(0, false); if (matched) searchFrom = groups[1]; return matched; }
    public boolean find() {
        int start = searchFrom;
        if (matched && groups[1] == groups[0]) start++;
        for (int s = start; s <= text.length(); s++) {
            if (run(s, false)) { matched = true; searchFrom = groups[1]; return true; }
        }
        matched = false;
        searchFrom = text.length() + 1;
        return false;
    }
    public boolean find(int start) {
        if (start < 0 || start > text.length()) throw new IndexOutOfBoundsException("Illegal start index");
        reset();
        searchFrom = start;
        return find();
    }
    private void check() { if (!matched) throw new IllegalStateException("No match found"); }
    public int start() { check(); return groups[0]; }
    public int end() { check(); return groups[1]; }
    public int start(int g) { check(); return groups[g * 2]; }
    public int end(int g) { check(); return groups[g * 2 + 1]; }
    public String group() { return group(0); }
    public String group(int g) {
        check();
        if (g < 0 || g > pattern.groups) throw new IndexOutOfBoundsException("No group " + g);
        int s = groups[g * 2], e = groups[g * 2 + 1];
        return s < 0 || e < 0 ? null : text.subSequence(s, e).toString();
    }
    public int groupCount() { return pattern.groups; }
    public boolean hitEnd() { return searchFrom >= text.length(); }
    public MatchResult toMatchResult() { return this; }
    public static String quoteReplacement(String s) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) { char c = s.charAt(i); if (c == '\\' || c == '$') b.append('\\'); b.append(c); }
        return b.toString();
    }
    private void appendExpanded(StringBuilder sb, String replacement) {
        for (int i = 0; i < replacement.length(); i++) {
            char c = replacement.charAt(i);
            if (c == '\\' && i + 1 < replacement.length()) sb.append(replacement.charAt(++i));
            else if (c == '$' && i + 1 < replacement.length() && Character.isDigit(replacement.charAt(i + 1))) {
                int g = replacement.charAt(++i) - '0';
                String v = group(g);
                if (v != null) sb.append(v);
            } else sb.append(c);
        }
    }
    public Matcher appendReplacement(StringBuffer sb, String replacement) {
        check();
        StringBuilder b = new StringBuilder();
        b.append(text, appendPos, start());
        appendExpanded(b, replacement);
        sb.append(b);
        appendPos = end();
        return this;
    }
    public StringBuffer appendTail(StringBuffer sb) { sb.append(text, appendPos, text.length()); return sb; }
    public String replaceAll(String replacement) {
        reset();
        StringBuilder b = new StringBuilder();
        int last = 0;
        while (find()) { b.append(text, last, start()); appendExpanded(b, replacement); last = end(); }
        return b.append(text, last, text.length()).toString();
    }
    public String replaceFirst(String replacement) {
        reset();
        if (!find()) return text.toString();
        StringBuilder b = new StringBuilder();
        b.append(text, 0, start());
        appendExpanded(b, replacement);
        return b.append(text, end(), text.length()).toString();
    }
}
