package java.io;

public class BufferedReader extends Reader {
    private Reader in;
    private char[] buf;
    private int pos, count, mark = -1;
    private boolean skipLF;

    public BufferedReader(Reader in) { this(in, 8192); }
    public BufferedReader(Reader in, int size) { super(in); this.in = in; buf = new char[size]; }
    private boolean fill() throws IOException {
        if (mark >= 0) {
            int keep = count - mark;
            if (keep >= buf.length) buf = java.util.Arrays.copyOf(buf, buf.length * 2);
            System.arraycopy(buf, mark, buf, 0, keep);
            pos -= mark; count = keep; mark = 0;
        } else { pos = 0; count = 0; }
        int n = in.read(buf, count, buf.length - count);
        if (n > 0) count += n;
        return n > 0;
    }
    public int read() throws IOException {
        for (;;) {
            if (pos >= count && !fill()) return -1;
            char c = buf[pos++];
            if (skipLF) { skipLF = false; if (c == '\n') continue; }
            return c;
        }
    }
    public int read(char[] c, int off, int len) throws IOException {
        if (len == 0) return 0;
        int n = 0;
        while (n < len) {
            if (pos >= count) { if (n > 0 && !in.ready()) break; if (!fill()) break; }
            if (skipLF) { skipLF = false; if (buf[pos] == '\n') { pos++; continue; } }
            int k = Math.min(len - n, count - pos);
            System.arraycopy(buf, pos, c, off + n, k);
            pos += k;
            n += k;
        }
        return n == 0 ? -1 : n;
    }
    public String readLine() throws IOException {
        StringBuilder b = null;
        for (;;) {
            if (pos >= count && !fill()) return b != null && b.length() > 0 ? b.toString() : b == null ? null : b.toString();
            if (skipLF) { skipLF = false; if (buf[pos] == '\n') pos++; if (pos >= count) continue; }
            int start = pos;
            while (pos < count && buf[pos] != '\n' && buf[pos] != '\r') pos++;
            if (b == null) b = new StringBuilder();
            b.append(buf, start, pos - start);
            if (pos < count) {
                char c = buf[pos++];
                if (c == '\r') skipLF = true;
                return b.toString();
            }
        }
    }
    public boolean ready() throws IOException { return pos < count || in.ready(); }
    public boolean markSupported() { return true; }
    public void mark(int limit) { mark = pos; }
    public void reset() throws IOException { if (mark < 0) throw new IOException("Stream not marked"); pos = mark; }
    public void close() throws IOException { in.close(); }
}
