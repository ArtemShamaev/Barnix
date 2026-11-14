package java.io;

public class PrintStream extends FilterOutputStream implements Appendable, Closeable {
    private final boolean autoFlush;
    private boolean error;
    public PrintStream(OutputStream out) { this(out, false); }
    public PrintStream(OutputStream out, boolean autoFlush) { super(out); this.autoFlush = autoFlush; }
    public PrintStream(OutputStream out, boolean autoFlush, String encoding) { this(out, autoFlush); }
    public PrintStream(String file) throws FileNotFoundException { this(new FileOutputStream(file)); }
    public PrintStream(File file) throws FileNotFoundException { this(new FileOutputStream(file)); }
    public void flush() { try { out.flush(); } catch (IOException e) { error = true; } }
    public void close() { try { out.close(); } catch (IOException e) { error = true; } }
    public boolean checkError() { flush(); return error; }
    protected void setError() { error = true; }
    public void write(int b) { try { out.write(b); if (b == '\n' && autoFlush) out.flush(); } catch (IOException e) { error = true; } }
    public void write(byte[] b, int off, int len) { try { out.write(b, off, len); if (autoFlush) out.flush(); } catch (IOException e) { error = true; } }
    private synchronized void text(String s) {
        try { byte[] b = s.getBytes(); out.write(b, 0, b.length); if (autoFlush && s.indexOf('\n') >= 0) out.flush(); }
        catch (IOException e) { error = true; }
    }
    public void print(boolean b) { text(b ? "true" : "false"); }
    public void print(char c) { text(String.valueOf(c)); }
    public void print(int i) { text(String.valueOf(i)); }
    public void print(long l) { text(String.valueOf(l)); }
    public void print(float f) { text(String.valueOf(f)); }
    public void print(double d) { text(String.valueOf(d)); }
    public void print(char[] s) { text(new String(s)); }
    public void print(String s) { text(s == null ? "null" : s); }
    public void print(Object o) { text(String.valueOf(o)); }
    public void println() { text("\n"); }
    public void println(boolean x) { text((x ? "true" : "false") + "\n"); }
    public void println(char x) { text(x + "\n"); }
    public void println(int x) { text(x + "\n"); }
    public void println(long x) { text(x + "\n"); }
    public void println(float x) { text(x + "\n"); }
    public void println(double x) { text(x + "\n"); }
    public void println(char[] x) { text(new String(x) + "\n"); }
    public void println(String x) { text(x + "\n"); }
    public void println(Object x) { text(String.valueOf(x) + "\n"); }
    public PrintStream printf(String format, Object... args) { return format(format, args); }
    public PrintStream printf(java.util.Locale l, String format, Object... args) { return format(format, args); }
    public PrintStream format(String format, Object... args) { text(new java.util.Formatter().format(format, args).toString()); return this; }
    public PrintStream format(java.util.Locale l, String format, Object... args) { return format(format, args); }
    public PrintStream append(CharSequence s) { print(String.valueOf(s)); return this; }
    public PrintStream append(CharSequence s, int start, int end) { print(String.valueOf(s == null ? "null" : s.subSequence(start, end))); return this; }
    public PrintStream append(char c) { print(c); return this; }
}
