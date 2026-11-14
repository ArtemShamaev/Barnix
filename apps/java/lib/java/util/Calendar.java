package java.util;

public abstract class Calendar implements java.io.Serializable, Cloneable, Comparable<Calendar> {
    public static final int ERA = 0, YEAR = 1, MONTH = 2, WEEK_OF_YEAR = 3, WEEK_OF_MONTH = 4, DATE = 5, DAY_OF_MONTH = 5,
        DAY_OF_YEAR = 6, DAY_OF_WEEK = 7, DAY_OF_WEEK_IN_MONTH = 8, AM_PM = 9, HOUR = 10, HOUR_OF_DAY = 11, MINUTE = 12,
        SECOND = 13, MILLISECOND = 14, ZONE_OFFSET = 15, DST_OFFSET = 16, FIELD_COUNT = 17;
    public static final int SUNDAY = 1, MONDAY = 2, TUESDAY = 3, WEDNESDAY = 4, THURSDAY = 5, FRIDAY = 6, SATURDAY = 7;
    public static final int JANUARY = 0, FEBRUARY = 1, MARCH = 2, APRIL = 3, MAY = 4, JUNE = 5, JULY = 6, AUGUST = 7,
        SEPTEMBER = 8, OCTOBER = 9, NOVEMBER = 10, DECEMBER = 11, AM = 0, PM = 1;
    protected int[] fields = new int[FIELD_COUNT];
    protected long time;
    protected TimeZone zone = TimeZone.getDefault();

    protected Calendar() {}
    public static Calendar getInstance() { return new GregorianCalendar(); }
    public static Calendar getInstance(TimeZone z) { Calendar c = new GregorianCalendar(); c.setTimeZone(z); return c; }
    public static Calendar getInstance(Locale l) { return new GregorianCalendar(); }
    public static Calendar getInstance(TimeZone z, Locale l) { return getInstance(z); }
    protected abstract void computeFields();
    protected abstract void computeTime();
    public int get(int f) { return fields[f]; }
    public void set(int f, int v) { fields[f] = v; computeTime(); computeFields(); }
    public final void set(int y, int m, int d) { fields[YEAR] = y; fields[MONTH] = m; fields[DAY_OF_MONTH] = d; computeTime(); computeFields(); }
    public final void set(int y, int m, int d, int h, int min) { fields[HOUR_OF_DAY] = h; fields[MINUTE] = min; set(y, m, d); }
    public final void set(int y, int m, int d, int h, int min, int s) { fields[SECOND] = s; set(y, m, d, h, min); }
    public void add(int f, int amount) {
        switch (f) {
        case MILLISECOND: setTimeInMillis(time + amount); break;
        case SECOND: setTimeInMillis(time + amount * 1000L); break;
        case MINUTE: setTimeInMillis(time + amount * 60000L); break;
        case HOUR: case HOUR_OF_DAY: setTimeInMillis(time + amount * 3600000L); break;
        case DAY_OF_MONTH: case DAY_OF_YEAR: case DAY_OF_WEEK: setTimeInMillis(time + amount * 86400000L); break;
        case WEEK_OF_YEAR: case WEEK_OF_MONTH: setTimeInMillis(time + amount * 604800000L); break;
        default: fields[f] += amount; computeTime(); computeFields();
        }
    }
    public final Date getTime() { return new Date(time); }
    public final void setTime(Date d) { setTimeInMillis(d.getTime()); }
    public long getTimeInMillis() { return time; }
    public void setTimeInMillis(long t) { time = t; computeFields(); }
    public TimeZone getTimeZone() { return zone; }
    public void setTimeZone(TimeZone z) { zone = z; computeFields(); }
    public void clear() { Arrays.fill(fields, 0); fields[DAY_OF_MONTH] = 1; fields[YEAR] = 1970; computeTime(); computeFields(); }
    public int getActualMaximum(int f) { return f == DAY_OF_MONTH ? GregorianCalendar.monthLength(fields[YEAR], fields[MONTH]) : getMaximum(f); }
    public int getMaximum(int f) { switch (f) { case MONTH: return 11; case DAY_OF_MONTH: return 31; case HOUR_OF_DAY: return 23; case MINUTE: case SECOND: return 59; default: return Integer.MAX_VALUE; } }
    public boolean before(Object o) { return o instanceof Calendar && time < ((Calendar) o).time; }
    public boolean after(Object o) { return o instanceof Calendar && time > ((Calendar) o).time; }
    public int compareTo(Calendar c) { return Long.compare(time, c.time); }
    public boolean equals(Object o) { return o instanceof Calendar && ((Calendar) o).time == time; }
    public int hashCode() { return (int) time; }
    public Object clone() { try { Calendar c = (Calendar) super.clone(); c.fields = fields.clone(); return c; } catch (CloneNotSupportedException e) { throw new InternalError(); } }
}
