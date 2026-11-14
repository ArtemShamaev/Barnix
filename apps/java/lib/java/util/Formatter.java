package java.util;

/* printf-style formatting: %[index$][flags][width][.precision]conv with
 * conversions b c d o x X e E f g s S h n % and flags - 0 + space , ( #. */
public final class Formatter implements java.io.Closeable, java.io.Flushable {
    private final StringBuilder out;
    private final Appendable target;

    public Formatter() { out = new StringBuilder(); target = null; }
    public Formatter(Appendable a) { out = new StringBuilder(); target = a; }
    public Formatter(Locale l) { this(); }
    public Formatter format(Locale l, String format, Object... args) { return format(format, args); }
    public Formatter format(String format, Object... args) {
        int next = 0, n = format.length();
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            char c = format.charAt(i);
            if (c != '%') { sb.append(c); continue; }
            int j = i + 1, index = -1;
            int k = j;
            while (k < n && Character.isDigit(format.charAt(k))) k++;
            if (k < n && format.charAt(k) == '$' && k > j) { index = Integer.parseInt(format.substring(j, k)) - 1; j = k + 1; }
            String flags = "";
            while (j < n && "-#+ 0,(<".indexOf(format.charAt(j)) >= 0) flags += format.charAt(j++);
            int width = -1, precision = -1;
            k = j;
            while (k < n && Character.isDigit(format.charAt(k))) k++;
            if (k > j) { width = Integer.parseInt(format.substring(j, k)); j = k; }
            if (j < n && format.charAt(j) == '.') {
                k = ++j;
                while (k < n && Character.isDigit(format.charAt(k))) k++;
                precision = Integer.parseInt(format.substring(j, k));
                j = k;
            }
            if (j >= n) throw new UnknownFormatConversionException("%");
            char conv = format.charAt(j);
            boolean upper = false;
            if (conv == 't' || conv == 'T') { j++; conv = format.charAt(j); }
            i = j;
            if (conv == 'n') { sb.append('\n'); continue; }
            if (conv == '%') { sb.append(pad("%", flags, width)); continue; }
            Object arg;
            if (flags.indexOf('<') >= 0) arg = args[Math.max(0, next - 1)];
            else if (index >= 0) arg = args[index];
            else { if (next >= args.length) throw new MissingFormatArgumentException("Format specifier '%" + conv + "'"); arg = args[next++]; }
            if (Character.isUpperCase(conv)) { upper = true; conv = Character.toLowerCase(conv); }
            String s;
            switch (conv) {
            case 'b': s = arg == null ? "false" : arg instanceof Boolean ? arg.toString() : "true"; break;
            case 'h': s = arg == null ? "null" : Integer.toHexString(arg.hashCode()); break;
            case 's': s = String.valueOf(arg); if (precision >= 0 && s.length() > precision) s = s.substring(0, precision); break;
            case 'c': s = arg == null ? "null" : arg instanceof Character ? arg.toString() : new String(Character.toChars(((Number) arg).intValue())); break;
            case 'd': s = arg == null ? "null" : integer(((Number) arg).longValue(), flags); break;
            case 'x': case 'o': {
                if (arg == null) { s = "null"; break; }
                long v = ((Number) arg).longValue();
                if (arg instanceof Integer) v &= 0xffffffffL; else if (arg instanceof Short) v &= 0xffff; else if (arg instanceof Byte) v &= 0xff;
                s = conv == 'x' ? Long.toHexString(v) : Long.toOctalString(v);
                if (flags.indexOf('#') >= 0) s = (conv == 'x' ? "0x" : "0") + s;
                if (flags.indexOf('0') >= 0 && width > s.length()) { StringBuilder z = new StringBuilder(); for (int q = s.length(); q < width; q++) z.append('0'); s = z + s; }
                break;
            }
            case 'f': case 'e': case 'g': case 'a':
                if (arg == null) { s = "null"; break; }
                s = real(((Number) arg).doubleValue(), conv, precision < 0 ? 6 : precision, flags);
                break;
            default: throw new UnknownFormatConversionException(String.valueOf(conv));
            }
            if (upper) s = s.toUpperCase();
            if (flags.indexOf('0') >= 0 && width > s.length() && "def".indexOf(conv) >= 0 && flags.indexOf('-') < 0) {
                int signLen = s.startsWith("-") || s.startsWith("+") || s.startsWith(" ") ? 1 : 0;
                StringBuilder z = new StringBuilder(s.substring(0, signLen));
                for (int q = s.length(); q < width; q++) z.append('0');
                s = z.append(s.substring(signLen)).toString();
            }
            sb.append(pad(s, flags, width));
        }
        out.append(sb);
        if (target != null) { try { target.append(sb); } catch (java.io.IOException e) {} }
        return this;
    }
    private static String pad(String s, String flags, int width) {
        if (width <= s.length()) return s;
        StringBuilder b = new StringBuilder();
        boolean left = flags.indexOf('-') >= 0;
        if (left) b.append(s);
        for (int i = s.length(); i < width; i++) b.append(' ');
        if (!left) b.append(s);
        return b.toString();
    }
    private static String group(String digits) {
        StringBuilder b = new StringBuilder();
        int n = digits.length();
        for (int i = 0; i < n; i++) { if (i > 0 && (n - i) % 3 == 0) b.append(','); b.append(digits.charAt(i)); }
        return b.toString();
    }
    private static String sign(boolean negative, String body, String flags) {
        if (negative) return flags.indexOf('(') >= 0 ? "(" + body + ")" : "-" + body;
        if (flags.indexOf('+') >= 0) return "+" + body;
        if (flags.indexOf(' ') >= 0) return " " + body;
        return body;
    }
    private static String integer(long v, String flags) {
        boolean neg = v < 0;
        String d = neg ? Long.toString(v).substring(1) : Long.toString(v);
        if (flags.indexOf(',') >= 0) d = group(d);
        return sign(neg, d, flags);
    }
    public static String fixedFormat(double v, int precision) { return fixed(v, precision); }
    static String fixed(double v, int precision) {
        /* round half up in decimal, using long arithmetic when it fits */
        double scale = Math.pow(10, precision);
        if (v * scale < 9.0e17) {
            long r = (long) Math.floor(v * scale + 0.5);
            String digits = Long.toString(r);
            if (precision == 0) return digits;
            while (digits.length() <= precision) digits = "0" + digits;
            return digits.substring(0, digits.length() - precision) + "." + digits.substring(digits.length() - precision);
        }
        String s = Double.toString(v);
        if (s.indexOf('E') >= 0) {
            java.math.BigDecimalLite d = new java.math.BigDecimalLite(s);
            return d.toPlain(precision);
        }
        return s;
    }
    private static String real(double v, char conv, int precision, String flags) {
        if (Double.isNaN(v)) return "NaN";
        if (Double.isInfinite(v)) return sign(v < 0, "Infinity", flags);
        boolean neg = v < 0 || (v == 0 && 1 / v < 0);
        double a = Math.abs(v);
        String body;
        if (conv == 'f') {
            body = fixed(a, precision);
            if (flags.indexOf(',') >= 0) { int dot = body.indexOf('.'); body = dot < 0 ? group(body) : group(body.substring(0, dot)) + body.substring(dot); }
        } else if (conv == 'e' || (conv == 'g' && (a != 0 && (a < 1e-4 || a >= Math.pow(10, precision == 0 ? 1 : precision))))) {
            int p = conv == 'g' ? Math.max(0, (precision == 0 ? 1 : precision) - 1) : precision;
            int exp = a == 0 ? 0 : (int) Math.floor(Math.log10(a));
            double m = a / Math.pow(10, exp);
            String ms = fixed(m, p);
            if (ms.startsWith("10")) { exp++; ms = fixed(a / Math.pow(10, exp), p); }
            body = ms + "e" + (exp < 0 ? "-" : "+") + (Math.abs(exp) < 10 ? "0" : "") + Math.abs(exp);
        } else if (conv == 'g') {
            int p = precision == 0 ? 1 : precision;
            int exp = a == 0 ? 0 : (int) Math.floor(Math.log10(a));
            body = fixed(a, Math.max(0, p - 1 - exp));
        } else body = Double.toHexString(a);
        return sign(neg, body, flags);
    }
    public String toString() { return target != null ? target.toString() : out.toString(); }
    public Appendable out() { return target != null ? target : out; }
    public void flush() {}
    public void close() {}
    public Locale locale() { return Locale.US; }
}
