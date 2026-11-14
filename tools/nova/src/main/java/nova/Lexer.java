package nova;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Set;

/** Превращает исходник в токены. Отступы превращаются в INDENT/DEDENT, как в Python. */
final class Lexer {
    static final Set<String> KEYWORDS = Set.of(
            "class", "init", "var", "if", "elif", "else", "while", "for", "in",
            "return", "break", "continue", "pass", "and", "or", "not",
            "true", "false", "null", "new", "this", "super",
            "int", "double", "bool", "string", "void");

    private static final String[] OPS = {
            "..", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=",
            "+", "-", "*", "/", "%", "<", ">", "=", "(", ")", "[", "]", ",", ".", ":"};

    private final String src;
    private int pos = 0, line = 1, lineStart = 0, depth = 0;
    private final List<Token> out = new ArrayList<>();
    private final Deque<Integer> indents = new ArrayDeque<>();

    Lexer(String src) {
        this.src = src.replace("\r\n", "\n");
    }

    private char ch() { return src.charAt(pos); }
    private char ch(int k) { return pos + k < src.length() ? src.charAt(pos + k) : '\0'; }
    private int col() { return pos - lineStart + 1; }

    private CompileError err(String msg) { return new CompileError(line, col(), msg); }

    private void add(Token.K k, String text, int col) { out.add(new Token(k, text, line, col)); }

    List<Token> run() {
        indents.push(0);
        boolean atLineStart = true;
        while (true) {
            if (atLineStart && depth == 0) {
                int ind = 0;
                while (pos < src.length() && (ch() == ' ' || ch() == '\t')) {
                    ind += ch() == '\t' ? 4 : 1;
                    pos++;
                }
                if (pos >= src.length()) break;
                if (ch() == '\n' || ch() == '\r' || src.startsWith("//", pos)) { skipLine(); continue; }
                if (ind > indents.peek()) {
                    indents.push(ind);
                    add(Token.K.INDENT, "", col());
                } else {
                    while (ind < indents.peek()) {
                        indents.pop();
                        add(Token.K.DEDENT, "", col());
                    }
                    if (ind != indents.peek()) throw err("отступ не совпадает ни с одним внешним уровнем");
                }
                atLineStart = false;
            }
            if (pos >= src.length()) break;
            char c = ch();
            if (c == '\n') {
                if (depth == 0) { add(Token.K.NEWLINE, "", col()); atLineStart = true; }
                pos++; line++; lineStart = pos;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') { pos++; continue; }
            if (src.startsWith("//", pos)) {
                while (pos < src.length() && ch() != '\n') pos++;
                continue;
            }
            int col = col();
            if (Character.isDigit(c)) { number(col); continue; }
            if (Character.isLetter(c) || c == '_') {
                int start = pos;
                while (pos < src.length() && (Character.isLetterOrDigit(ch()) || ch() == '_')) pos++;
                String w = src.substring(start, pos);
                add(KEYWORDS.contains(w) ? Token.K.KW : Token.K.IDENT, w, col);
                continue;
            }
            if (c == '"') { string(col); continue; }
            boolean matched = false;
            for (String op : OPS) {
                if (src.startsWith(op, pos)) {
                    if (op.equals("(") || op.equals("[")) depth++;
                    if (op.equals(")") || op.equals("]")) depth = Math.max(0, depth - 1);
                    add(Token.K.OP, op, col);
                    pos += op.length();
                    matched = true;
                    break;
                }
            }
            if (!matched) throw err("неожиданный символ '" + c + "'");
        }
        if (!out.isEmpty() && out.get(out.size() - 1).kind != Token.K.NEWLINE
                && out.get(out.size() - 1).kind != Token.K.DEDENT) {
            add(Token.K.NEWLINE, "", col());
        }
        while (indents.peek() > 0) {
            indents.pop();
            add(Token.K.DEDENT, "", col());
        }
        add(Token.K.EOF, "", col());
        return out;
    }

    private void skipLine() {
        while (pos < src.length() && ch() != '\n') pos++;
        if (pos < src.length()) { pos++; line++; lineStart = pos; }
    }

    private void number(int col) {
        int start = pos;
        while (pos < src.length() && Character.isDigit(ch())) pos++;
        boolean isDouble = false;
        if (pos < src.length() && ch() == '.' && Character.isDigit(ch(1))) {
            isDouble = true;
            pos++;
            while (pos < src.length() && Character.isDigit(ch())) pos++;
        }
        String text = src.substring(start, pos);
        if (isDouble) {
            Token t = new Token(Token.K.DOUBLE, text, line, col);
            t.value = Double.parseDouble(text);
            out.add(t);
        } else {
            long v;
            try { v = Long.parseLong(text); } catch (NumberFormatException e) { v = Long.MAX_VALUE; }
            if (v > Integer.MAX_VALUE) throw new CompileError(line, col, "число " + text + " не помещается в int");
            Token t = new Token(Token.K.INT, text, line, col);
            t.value = (int) v;
            out.add(t);
        }
    }

    private void string(int col) {
        pos++; // открывающая кавычка
        StringBuilder sb = new StringBuilder();
        while (true) {
            if (pos >= src.length() || ch() == '\n') throw new CompileError(line, col, "незакрытая строка");
            char c = ch();
            if (c == '"') { pos++; break; }
            if (c == '\\') {
                char e = ch(1);
                switch (e) {
                    case 'n' -> sb.append('\n');
                    case 't' -> sb.append('\t');
                    case 'r' -> sb.append('\r');
                    case '0' -> sb.append('\0');
                    case '"' -> sb.append('"');
                    case '\\' -> sb.append('\\');
                    default -> throw err("неизвестная escape-последовательность \\" + e);
                }
                pos += 2;
                continue;
            }
            sb.append(c);
            pos++;
        }
        out.add(new Token(Token.K.STRING, sb.toString(), line, col));
    }
}
