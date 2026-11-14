package java.text;

import java.util.Calendar;
import java.util.Date;

public class SimpleDateFormat extends DateFormat {
    private static final String[] MONTHS = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    private static final String[] DAYS = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    private String pattern;
    public SimpleDateFormat() { this("M/d/yy h:mm a"); }
    public SimpleDateFormat(String pattern) { this.pattern = pattern; }
    public SimpleDateFormat(String pattern, java.util.Locale l) { this(pattern); }
    public String toPattern() { return pattern; }
    public void applyPattern(String p) { pattern = p; }
    private static String num(int v, int width) { String s = Integer.toString(v); while (s.length() < width) s = "0" + s; return s; }
    public StringBuffer format(Date d, StringBuffer out, FieldPosition pos) {
        Calendar c = calendar;
        c.setTime(d);
        int n = pattern.length();
        for (int i = 0; i < n;) {
            char ch = pattern.charAt(i);
            if (ch == '\'') {
                int end = pattern.indexOf('\'', i + 1);
                if (end == i + 1) { out.append('\''); i += 2; continue; }
                if (end < 0) end = n;
                out.append(pattern, i + 1, end);
                i = end + 1;
                continue;
            }
            if (!(ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z')) { out.append(ch); i++; continue; }
            int k = i;
            while (k < n && pattern.charAt(k) == ch) k++;
            int w = k - i;
            i = k;
            switch (ch) {
            case 'y': { int y = c.get(Calendar.YEAR); out.append(w == 2 ? num(y % 100, 2) : num(y, w)); break; }
            case 'M': { int m = c.get(Calendar.MONTH); out.append(w >= 4 ? MONTHS[m] : w == 3 ? MONTHS[m].substring(0, 3) : num(m + 1, w)); break; }
            case 'd': out.append(num(c.get(Calendar.DAY_OF_MONTH), w)); break;
            case 'D': out.append(num(c.get(Calendar.DAY_OF_YEAR), w)); break;
            case 'E': { String day = DAYS[c.get(Calendar.DAY_OF_WEEK) - 1]; out.append(w >= 4 ? day : day.substring(0, 3)); break; }
            case 'H': out.append(num(c.get(Calendar.HOUR_OF_DAY), w)); break;
            case 'k': { int h = c.get(Calendar.HOUR_OF_DAY); out.append(num(h == 0 ? 24 : h, w)); break; }
            case 'h': { int h = c.get(Calendar.HOUR); out.append(num(h == 0 ? 12 : h, w)); break; }
            case 'K': out.append(num(c.get(Calendar.HOUR), w)); break;
            case 'm': out.append(num(c.get(Calendar.MINUTE), w)); break;
            case 's': out.append(num(c.get(Calendar.SECOND), w)); break;
            case 'S': out.append(num(c.get(Calendar.MILLISECOND), w)); break;
            case 'a': out.append(c.get(Calendar.AM_PM) == Calendar.AM ? "AM" : "PM"); break;
            case 'z': case 'Z': case 'X': out.append(ch == 'z' ? c.getTimeZone().getID() : "+0000"); break;
            default: for (int q = 0; q < w; q++) out.append(ch);
            }
        }
        return out;
    }
}
