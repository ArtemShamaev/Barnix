package java.util;

public class TimeZone implements java.io.Serializable, Cloneable {
    private static TimeZone defaultZone;
    private final String id;
    private final int offset;
    protected TimeZone(String id, int offset) { this.id = id; this.offset = offset; }
    public TimeZone() { this("UTC", 0); }
    public static TimeZone getTimeZone(String id) {
        if (id.startsWith("GMT") && id.length() > 4) {
            int sign = id.charAt(3) == '-' ? -1 : 1;
            String[] p = id.substring(4).split(":");
            try { int h = Integer.parseInt(p[0]), m = p.length > 1 ? Integer.parseInt(p[1]) : 0; return new TimeZone(id, sign * (h * 60 + m) * 60000); }
            catch (NumberFormatException e) {}
        }
        return new TimeZone(id, 0);
    }
    public static TimeZone getDefault() {
        if (defaultZone == null) {
            String z = System.getProperty("user.timezone");
            defaultZone = z != null ? getTimeZone(z) : new TimeZone("UTC", 0);
        }
        return defaultZone;
    }
    public static void setDefault(TimeZone z) { defaultZone = z; }
    public String getID() { return id; }
    public int getRawOffset() { return offset; }
    public int getOffset(long date) { return offset; }
    public boolean inDaylightTime(Date d) { return false; }
    public boolean useDaylightTime() { return false; }
    public String getDisplayName() { return id; }
}
