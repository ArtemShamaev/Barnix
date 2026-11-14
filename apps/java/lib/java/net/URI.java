package java.net;

public final class URI implements Comparable<URI>, java.io.Serializable {
    private final String text, scheme, host, path, query, fragment;
    private final int port;
    public URI(String s) throws URISyntaxException {
        if (s == null) throw new NullPointerException();
        text = s;
        String rest = s;
        int hash = rest.indexOf('#');
        fragment = hash >= 0 ? rest.substring(hash + 1) : null;
        if (hash >= 0) rest = rest.substring(0, hash);
        int colon = rest.indexOf(':');
        int slash = rest.indexOf('/');
        if (colon > 0 && (slash < 0 || colon < slash)) { scheme = rest.substring(0, colon); rest = rest.substring(colon + 1); } else scheme = null;
        String h = null;
        int p = -1;
        if (rest.startsWith("//")) {
            rest = rest.substring(2);
            int end = rest.indexOf('/');
            String authority = end < 0 ? rest : rest.substring(0, end);
            rest = end < 0 ? "" : rest.substring(end);
            int at = authority.lastIndexOf('@');
            if (at >= 0) authority = authority.substring(at + 1);
            int pc = authority.lastIndexOf(':');
            if (pc >= 0) { try { p = Integer.parseInt(authority.substring(pc + 1)); } catch (NumberFormatException e) { throw new URISyntaxException(s, "Illegal port"); } h = authority.substring(0, pc); }
            else h = authority;
        }
        int q = rest.indexOf('?');
        query = q >= 0 ? rest.substring(q + 1) : null;
        path = q >= 0 ? rest.substring(0, q) : rest;
        host = h;
        port = p;
    }
    public static URI create(String s) { try { return new URI(s); } catch (URISyntaxException e) { throw new IllegalArgumentException(e.getMessage()); } }
    public String getScheme() { return scheme; }
    public String getHost() { return host; }
    public int getPort() { return port; }
    public String getPath() { return path; }
    public String getRawPath() { return path; }
    public String getQuery() { return query; }
    public String getFragment() { return fragment; }
    public boolean isAbsolute() { return scheme != null; }
    public URL toURL() throws MalformedURLException { if (scheme == null) throw new IllegalArgumentException("URI is not absolute"); return new URL(text); }
    public URI resolve(String s) { return create(s); }
    public String toString() { return text; }
    public String toASCIIString() { return text; }
    public boolean equals(Object o) { return o instanceof URI && ((URI) o).text.equals(text); }
    public int hashCode() { return text.hashCode(); }
    public int compareTo(URI o) { return text.compareTo(o.text); }
}
