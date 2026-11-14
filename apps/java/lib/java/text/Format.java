package java.text;

public abstract class Format implements java.io.Serializable, Cloneable {
    protected Format() {}
    public final String format(Object o) { return format(o, new StringBuffer(), null).toString(); }
    public abstract StringBuffer format(Object o, StringBuffer to, FieldPosition pos);
    public Object parseObject(String s) throws ParseException { throw new ParseException(s, 0); }
}
