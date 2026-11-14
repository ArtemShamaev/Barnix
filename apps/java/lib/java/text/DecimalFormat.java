package java.text;

public class DecimalFormat extends NumberFormat {
    private boolean percent;
    private int minInteger = 1;
    public DecimalFormat() { this("#,##0.###"); }
    public DecimalFormat(String pattern) { applyPattern(pattern); }
    public void applyPattern(String p) {
        percent = p.indexOf('%') >= 0;
        grouping = p.indexOf(',') >= 0;
        int dot = p.indexOf('.');
        String intPart = dot < 0 ? p : p.substring(0, dot);
        minInteger = 0;
        for (int i = 0; i < intPart.length(); i++) if (intPart.charAt(i) == '0') minInteger++;
        minFraction = maxFraction = 0;
        if (dot >= 0) for (int i = dot + 1; i < p.length(); i++) { char c = p.charAt(i); if (c == '0') { minFraction++; maxFraction++; } else if (c == '#') maxFraction++; }
    }
    public StringBuffer format(double d, StringBuffer out, FieldPosition pos) {
        if (percent) d *= 100;
        boolean neg = d < 0;
        String s = java.util.Formatter.fixedFormat(Math.abs(d), maxFraction);
        int dot = s.indexOf('.');
        String ip = dot < 0 ? s : s.substring(0, dot), fp = dot < 0 ? "" : s.substring(dot + 1);
        while (fp.length() > minFraction && fp.endsWith("0")) fp = fp.substring(0, fp.length() - 1);
        while (ip.length() < minInteger) ip = "0" + ip;
        if (minInteger == 0 && ip.equals("0") && fp.length() > 0) ip = "";
        if (grouping) { StringBuilder g = new StringBuilder(); for (int i = 0; i < ip.length(); i++) { if (i > 0 && (ip.length() - i) % 3 == 0) g.append(','); g.append(ip.charAt(i)); } ip = g.toString(); }
        if (neg && (ip.length() > 0 && !ip.matches("[0,]*") || fp.matches(".*[1-9].*"))) out.append('-');
        out.append(ip);
        if (fp.length() > 0) out.append('.').append(fp);
        if (percent) out.append('%');
        return out;
    }
}
