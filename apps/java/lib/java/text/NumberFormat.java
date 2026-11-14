package java.text;

public abstract class NumberFormat extends Format {
    protected int minFraction = 0, maxFraction = 3;
    protected boolean grouping = true;
    public static NumberFormat getInstance() { return new DecimalFormat("#,##0.###"); }
    public static NumberFormat getNumberInstance() { return getInstance(); }
    public static NumberFormat getInstance(java.util.Locale l) { return getInstance(); }
    public static NumberFormat getIntegerInstance() { return new DecimalFormat("#,##0"); }
    public static NumberFormat getPercentInstance() { return new DecimalFormat("#,##0%"); }
    public final String format(double d) { return format(d, new StringBuffer(), null).toString(); }
    public final String format(long l) { return format((double) l, new StringBuffer(), null).toString(); }
    public abstract StringBuffer format(double d, StringBuffer to, FieldPosition pos);
    public StringBuffer format(Object o, StringBuffer to, FieldPosition pos) { return format(((Number) o).doubleValue(), to, pos); }
    public Number parse(String s) throws ParseException { try { return Double.valueOf(s.replace(",", "")); } catch (NumberFormatException e) { throw new ParseException(s, 0); } }
    public void setMinimumFractionDigits(int n) { minFraction = n; if (maxFraction < n) maxFraction = n; }
    public void setMaximumFractionDigits(int n) { maxFraction = n; if (minFraction > n) minFraction = n; }
    public void setGroupingUsed(boolean g) { grouping = g; }
    public void setMinimumIntegerDigits(int n) {}
    public void setMaximumIntegerDigits(int n) {}
}
