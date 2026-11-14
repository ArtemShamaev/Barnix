package java.util;

public class GregorianCalendar extends Calendar {
    public static final int BC = 0, AD = 1;
    public GregorianCalendar() { setTimeInMillis(System.currentTimeMillis()); }
    public GregorianCalendar(int y, int m, int d) { this(y, m, d, 0, 0, 0); }
    public GregorianCalendar(int y, int m, int d, int h, int min) { this(y, m, d, h, min, 0); }
    public GregorianCalendar(int y, int m, int d, int h, int min, int s) {
        fields[YEAR] = y; fields[MONTH] = m; fields[DAY_OF_MONTH] = d; fields[HOUR_OF_DAY] = h; fields[MINUTE] = min; fields[SECOND] = s;
        computeTime(); computeFields();
    }
    public GregorianCalendar(TimeZone z) { zone = z; setTimeInMillis(System.currentTimeMillis()); }
    public GregorianCalendar(Locale l) { this(); }
    public boolean isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
    static int monthLength(int y, int m) {
        int[] n = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : n[m];
    }
    private static long daysFromCivil(long y, int m, int d) {
        y -= m <= 2 ? 1 : 0;
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }
    protected void computeTime() {
        int y = fields[YEAR], m = fields[MONTH];
        y += Math.floorDiv(m, 12);
        m = Math.floorMod(m, 12);
        long days = daysFromCivil(y, m + 1, 1) + fields[DAY_OF_MONTH] - 1;
        time = days * 86400000L + fields[HOUR_OF_DAY] * 3600000L + fields[MINUTE] * 60000L + fields[SECOND] * 1000L + fields[MILLISECOND] - zone.getOffset(0);
    }
    protected void computeFields() {
        long local = time + zone.getOffset(time);
        long days = Math.floorDiv(local, 86400000L), ms = Math.floorMod(local, 86400000L);
        long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp + (mp < 10 ? 3 : -9), y = yoe + era * 400 + (m <= 2 ? 1 : 0);
        fields[ERA] = y > 0 ? AD : BC;
        fields[YEAR] = (int) y;
        fields[MONTH] = (int) m - 1;
        fields[DAY_OF_MONTH] = (int) d;
        fields[DAY_OF_WEEK] = (int) Math.floorMod(days + 4, 7L) + 1;
        fields[DAY_OF_YEAR] = (int) (days - daysFromCivil(y, 1, 1)) + 1;
        fields[HOUR_OF_DAY] = (int) (ms / 3600000);
        fields[HOUR] = fields[HOUR_OF_DAY] % 12;
        fields[AM_PM] = fields[HOUR_OF_DAY] >= 12 ? PM : AM;
        fields[MINUTE] = (int) (ms / 60000 % 60);
        fields[SECOND] = (int) (ms / 1000 % 60);
        fields[MILLISECOND] = (int) (ms % 1000);
        fields[WEEK_OF_YEAR] = (fields[DAY_OF_YEAR] - 1) / 7 + 1;
        fields[WEEK_OF_MONTH] = (fields[DAY_OF_MONTH] - 1) / 7 + 1;
        fields[DAY_OF_WEEK_IN_MONTH] = (fields[DAY_OF_MONTH] - 1) / 7 + 1;
        fields[ZONE_OFFSET] = zone.getRawOffset();
    }
}
