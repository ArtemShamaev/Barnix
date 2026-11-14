package java.util;

public class Date implements java.io.Serializable, Cloneable, Comparable<Date> {
    private long time;
    public Date() { time = System.currentTimeMillis(); }
    public Date(long t) { time = t; }
    @Deprecated public Date(int year, int month, int day) { GregorianCalendar c = new GregorianCalendar(year + 1900, month, day); time = c.getTimeInMillis(); }
    public long getTime() { return time; }
    public void setTime(long t) { time = t; }
    public boolean before(Date d) { return time < d.time; }
    public boolean after(Date d) { return time > d.time; }
    public boolean equals(Object o) { return o instanceof Date && ((Date) o).time == time; }
    public int hashCode() { return (int) time ^ (int) (time >> 32); }
    public int compareTo(Date d) { return Long.compare(time, d.time); }
    public Object clone() { return new Date(time); }
    private int field(int f) { Calendar c = Calendar.getInstance(); c.setTime(this); return c.get(f); }
    @Deprecated public int getYear() { return field(Calendar.YEAR) - 1900; }
    @Deprecated public int getMonth() { return field(Calendar.MONTH); }
    @Deprecated public int getDate() { return field(Calendar.DAY_OF_MONTH); }
    @Deprecated public int getDay() { return field(Calendar.DAY_OF_WEEK) - 1; }
    @Deprecated public int getHours() { return field(Calendar.HOUR_OF_DAY); }
    @Deprecated public int getMinutes() { return field(Calendar.MINUTE); }
    @Deprecated public int getSeconds() { return field(Calendar.SECOND); }
    public String toString() { return new java.text.SimpleDateFormat("EEE MMM dd HH:mm:ss zzz yyyy").format(this); }
}
