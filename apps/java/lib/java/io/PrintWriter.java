package java.io;

public class PrintWriter extends Writer {
    protected Writer out;
    private final boolean autoFlush;
    private boolean error;
    public PrintWriter(Writer out) { this(out, false); }
    public PrintWriter(Writer out, boolean autoFlush) { super(out); this.out = out; this.autoFlush = autoFlush; }
    public PrintWriter(OutputStream out) { this(new OutputStreamWriter(out), false); }
    public PrintWriter(OutputStream out, boolean autoFlush) { this(new OutputStreamWriter(out), autoFlush); }
    public PrintWriter(String file) throws FileNotFoundException { this(new OutputStreamWriter(new FileOutputStream(file))); }
    public PrintWriter(File file) throws FileNotFoundException { this(new OutputStreamWriter(new FileOutputStream(file))); }
    public void flush() { try { out.flush(); } catch (IOException e) { error = true; } }
    public void close() { try { out.close(); } catch (IOException e) { error = true; } }
    public boolean checkError() { flush(); return error; }
    public void write(int c) { try { out.write(c); } catch (IOException e) { error = true; } }
    public void write(char[] c, int off, int len) { try { out.write(c, off, len); } catch (IOException e) { error = true; } }
    public void write(char[] c) { write(c, 0, c.length); }
    public void write(String s, int off, int len) { try { out.write(s, off, len); } catch (IOException e) { error = true; } }
    public void write(String s) { write(s, 0, s.length()); }
    public void print(boolean b) { write(b ? "true" : "false"); }
    public void print(char c) { write(c); }
    public void print(int i) { write(String.valueOf(i)); }
    public void print(long l) { write(String.valueOf(l)); }
    public void print(float f) { write(String.valueOf(f)); }
    public void print(double d) { write(String.valueOf(d)); }
    public void print(char[] s) { write(s); }
    public void print(String s) { write(s == null ? "null" : s); }
    public void print(Object o) { write(String.valueOf(o)); }
    public void println() { write('\n'); if (autoFlush) flush(); }
    public void println(boolean x) { print(x); println(); }
    public void println(char x) { print(x); println(); }
    public void println(int x) { print(x); println(); }
    public void println(long x) { print(x); println(); }
    public void println(float x) { print(x); println(); }
    public void println(double x) { print(x); println(); }
    public void println(char[] x) { print(x); println(); }
    public void println(String x) { print(x); println(); }
    public void println(Object x) { print(String.valueOf(x)); println(); }
    public PrintWriter printf(String format, Object... args) { return format(format, args); }
    public PrintWriter format(String format, Object... args) { write(new java.util.Formatter().format(format, args).toString()); if (autoFlush) flush(); return this; }
    public PrintWriter append(CharSequence s) { write(String.valueOf(s)); return this; }
    public PrintWriter append(char c) { write(c); return this; }
}
