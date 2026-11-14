package nova;

final class CompileError extends RuntimeException {
    final int line, col;

    CompileError(int line, int col, String message) {
        super(message);
        this.line = line;
        this.col = col;
    }
}
