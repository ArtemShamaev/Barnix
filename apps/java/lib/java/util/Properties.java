package java.util;

import java.io.*;

public class Properties extends Hashtable<Object, Object> {
    protected Properties defaults;
    public Properties() {}
    public Properties(Properties defaults) { this.defaults = defaults; }
    public synchronized Object setProperty(String key, String value) { return put(key, value); }
    public String getProperty(String key) {
        Object v = get(key);
        String s = v instanceof String ? (String) v : null;
        return s == null && defaults != null ? defaults.getProperty(key) : s;
    }
    public String getProperty(String key, String def) { String v = getProperty(key); return v == null ? def : v; }
    public Enumeration<?> propertyNames() { return keys(); }
    public Set<String> stringPropertyNames() {
        Set<String> s = new HashSet<String>();
        if (defaults != null) s.addAll(defaults.stringPropertyNames());
        for (Object k : keySet()) if (k instanceof String) s.add((String) k);
        return s;
    }
    public synchronized void load(InputStream in) throws IOException { load(new InputStreamReader(in, "ISO-8859-1")); }
    public synchronized void load(Reader reader) throws IOException {
        BufferedReader r = new BufferedReader(reader);
        String line;
        StringBuilder pending = null;
        while ((line = r.readLine()) != null) {
            String s = pending != null ? pending + trimLeft(line) : trimLeft(line);
            pending = null;
            if (s.length() == 0 || s.charAt(0) == '#' || s.charAt(0) == '!') continue;
            int slashes = 0;
            for (int i = s.length() - 1; i >= 0 && s.charAt(i) == '\\'; i--) slashes++;
            if (slashes % 2 == 1) { pending = new StringBuilder(s.substring(0, s.length() - 1)); continue; }
            int i = 0;
            StringBuilder key = new StringBuilder();
            while (i < s.length()) {
                char c = s.charAt(i);
                if (c == '\\' && i + 1 < s.length()) { i++; key.append(unescape(s, i)); i = skip; continue; }
                if (c == '=' || c == ':' || c == ' ' || c == '\t' || c == '\f') break;
                key.append(c); i++;
            }
            while (i < s.length() && (s.charAt(i) == ' ' || s.charAt(i) == '\t' || s.charAt(i) == '\f')) i++;
            if (i < s.length() && (s.charAt(i) == '=' || s.charAt(i) == ':')) i++;
            while (i < s.length() && (s.charAt(i) == ' ' || s.charAt(i) == '\t' || s.charAt(i) == '\f')) i++;
            StringBuilder value = new StringBuilder();
            while (i < s.length()) {
                char c = s.charAt(i);
                if (c == '\\' && i + 1 < s.length()) { i++; value.append(unescape(s, i)); i = skip; continue; }
                value.append(c); i++;
            }
            put(key.toString(), value.toString());
        }
    }
    private int skip;
    private char unescape(String s, int i) {
        char c = s.charAt(i);
        skip = i + 1;
        switch (c) {
        case 't': return '\t';
        case 'n': return '\n';
        case 'r': return '\r';
        case 'f': return '\f';
        case 'u':
            if (i + 5 <= s.length()) { skip = i + 5; return (char) Integer.parseInt(s.substring(i + 1, i + 5), 16); }
            return 'u';
        default: return c;
        }
    }
    private static String trimLeft(String s) { int i = 0; while (i < s.length() && (s.charAt(i) == ' ' || s.charAt(i) == '\t' || s.charAt(i) == '\f')) i++; return s.substring(i); }
    public void store(OutputStream out, String comments) throws IOException { store(new OutputStreamWriter(out, "ISO-8859-1"), comments); }
    public void store(Writer w, String comments) throws IOException {
        BufferedWriter b = new BufferedWriter(w);
        if (comments != null) { b.write("#" + comments); b.newLine(); }
        b.write("#" + new Date()); b.newLine();
        for (Map.Entry<Object, Object> e : entrySet()) { b.write(escape(e.getKey().toString(), true) + "=" + escape(e.getValue().toString(), false)); b.newLine(); }
        b.flush();
    }
    private static String escape(String s, boolean key) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
            case '\\': b.append("\\\\"); break;
            case '\t': b.append("\\t"); break;
            case '\n': b.append("\\n"); break;
            case '\r': b.append("\\r"); break;
            case '=': case ':': case '#': case '!': b.append('\\').append(c); break;
            case ' ': if (key || i == 0) b.append('\\'); b.append(' '); break;
            default: if (c < 0x20 || c > 0x7e) { String h = Integer.toHexString(c); b.append("\\u"); for (int k = h.length(); k < 4; k++) b.append('0'); b.append(h); } else b.append(c);
            }
        }
        return b.toString();
    }
    public void list(PrintStream out) { for (Map.Entry<Object, Object> e : entrySet()) out.println(e.getKey() + "=" + e.getValue()); }
}
