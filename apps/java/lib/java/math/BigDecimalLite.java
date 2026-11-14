package java.math;

/* Decimal digits of a number in E notation, for fixed-point printing of large values. */
public final class BigDecimalLite {
    private final String digits;
    private final int pointAt;
    public BigDecimalLite(String s) {
        int e = s.indexOf('E');
        String m = s.substring(0, e);
        int exp = Integer.parseInt(s.substring(e + 1));
        int dot = m.indexOf('.');
        String d = dot < 0 ? m : m.substring(0, dot) + m.substring(dot + 1);
        digits = d;
        pointAt = (dot < 0 ? m.length() : dot) + exp;
    }
    public String toPlain(int precision) {
        StringBuilder b = new StringBuilder();
        if (pointAt <= 0) { b.append("0"); } else for (int i = 0; i < pointAt; i++) b.append(i < digits.length() ? digits.charAt(i) : '0');
        if (precision > 0) {
            b.append('.');
            for (int i = 0; i < precision; i++) { int k = pointAt + i; b.append(k >= 0 && k < digits.length() ? digits.charAt(k) : '0'); }
        }
        return b.toString();
    }
}
