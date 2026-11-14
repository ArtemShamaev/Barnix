package nova;

final class Token {
    enum K { INT, DOUBLE, STRING, IDENT, KW, OP, NEWLINE, INDENT, DEDENT, EOF }

    final K kind;
    final String text;
    final int line, col;
    Object value;

    Token(K kind, String text, int line, int col) {
        this.kind = kind;
        this.text = text;
        this.line = line;
        this.col = col;
    }

    boolean is(String s) {
        return (kind == K.KW || kind == K.OP) && text.equals(s);
    }

    @Override
    public String toString() {
        return switch (kind) {
            case NEWLINE -> "конец строки";
            case INDENT -> "лишний отступ";
            case DEDENT -> "конец блока";
            case EOF -> "конец файла";
            case STRING -> "строка \"" + text + "\"";
            default -> "'" + text + "'";
        };
    }
}
