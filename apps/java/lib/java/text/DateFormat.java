package java.text;

import java.util.Date;

public abstract class DateFormat extends Format {
    public static final int FULL = 0, LONG = 1, MEDIUM = 2, SHORT = 3, DEFAULT = MEDIUM;
    protected java.util.Calendar calendar = java.util.Calendar.getInstance();
    protected DateFormat() {}
    public abstract StringBuffer format(Date d, StringBuffer to, FieldPosition pos);
    public StringBuffer format(Object o, StringBuffer to, FieldPosition pos) {
        if (o instanceof Date) return format((Date) o, to, pos);
        if (o instanceof Number) return format(new Date(((Number) o).longValue()), to, pos);
        throw new IllegalArgumentException("Cannot format given Object as a Date");
    }
    public final String format(Date d) { return format(d, new StringBuffer(), null).toString(); }
    public Date parse(String s) throws ParseException { throw new ParseException("Unparseable date: \"" + s + "\"", 0); }
    public static final DateFormat getDateInstance() { return new SimpleDateFormat("MMM d, yyyy"); }
    public static final DateFormat getDateInstance(int style) { return new SimpleDateFormat(style == SHORT ? "M/d/yy" : "MMM d, yyyy"); }
    public static final DateFormat getDateInstance(int style, java.util.Locale l) { return getDateInstance(style); }
    public static final DateFormat getTimeInstance() { return new SimpleDateFormat("h:mm:ss a"); }
    public static final DateFormat getTimeInstance(int style) { return new SimpleDateFormat(style == SHORT ? "h:mm a" : "h:mm:ss a"); }
    public static final DateFormat getDateTimeInstance() { return new SimpleDateFormat("MMM d, yyyy h:mm:ss a"); }
    public static final DateFormat getDateTimeInstance(int a, int b) { return new SimpleDateFormat(a == SHORT ? "M/d/yy h:mm a" : "MMM d, yyyy h:mm:ss a"); }
    public static final DateFormat getDateTimeInstance(int a, int b, java.util.Locale l) { return getDateTimeInstance(a, b); }
    public static final DateFormat getInstance() { return getDateTimeInstance(SHORT, SHORT); }
    public void setTimeZone(java.util.TimeZone z) { calendar.setTimeZone(z); }
    public java.util.TimeZone getTimeZone() { return calendar.getTimeZone(); }
    public void setLenient(boolean b) {}
}
