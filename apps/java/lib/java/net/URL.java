package java.net;

import java.io.IOException;
import java.io.InputStream;

public final class URL implements java.io.Serializable {
    private final String protocol, host, file, ref;
    private final int port;
    public URL(String spec) throws MalformedURLException { this(null, spec); }
    public URL(URL context, String spec) throws MalformedURLException {
        if (spec == null) throw new MalformedURLException("null");
        int colon = spec.indexOf(':'), slash = spec.indexOf('/');
        if (colon > 0 && (slash < 0 || colon < slash)) {
            protocol = spec.substring(0, colon).toLowerCase();
            String rest = spec.substring(colon + 1);
            String h = "";
            int p = -1;
            if (rest.startsWith("//")) {
                rest = rest.substring(2);
                int end = rest.indexOf('/');
                String authority = end < 0 ? rest : rest.substring(0, end);
                rest = end < 0 ? "" : rest.substring(end);
                int pc = authority.lastIndexOf(':');
                if (pc >= 0 && authority.indexOf(']') < pc) { try { p = Integer.parseInt(authority.substring(pc + 1)); } catch (NumberFormatException e) { throw new MalformedURLException("Invalid port: " + spec); } h = authority.substring(0, pc); }
                else h = authority;
            }
            int hash = rest.indexOf('#');
            ref = hash >= 0 ? rest.substring(hash + 1) : null;
            file = hash >= 0 ? rest.substring(0, hash) : rest;
            host = h;
            port = p;
        } else if (context != null) {
            protocol = context.protocol; host = context.host; port = context.port; ref = null;
            if (spec.startsWith("/")) file = spec;
            else { String base = context.file; int i = base.lastIndexOf('/'); file = (i >= 0 ? base.substring(0, i + 1) : "/") + spec; }
        } else throw new MalformedURLException("no protocol: " + spec);
        if (!(protocol.equals("http") || protocol.equals("https") || protocol.equals("file") || protocol.equals("classpath") || protocol.equals("jar") || protocol.equals("ftp")))
            throw new MalformedURLException("unknown protocol: " + protocol);
    }
    public URL(String protocol, String host, int port, String file) throws MalformedURLException { this(protocol + "://" + host + (port >= 0 ? ":" + port : "") + file); }
    public URL(String protocol, String host, String file) throws MalformedURLException { this(protocol, host, -1, file); }
    public String getProtocol() { return protocol; }
    public String getHost() { return host; }
    public int getPort() { return port; }
    public int getDefaultPort() { return protocol.equals("https") ? 443 : protocol.equals("http") ? 80 : -1; }
    public String getFile() { return file; }
    public String getPath() { int q = file.indexOf('?'); return q < 0 ? file : file.substring(0, q); }
    public String getQuery() { int q = file.indexOf('?'); return q < 0 ? null : file.substring(q + 1); }
    public String getRef() { return ref; }
    public String getAuthority() { return port >= 0 ? host + ":" + port : host; }
    public String toExternalForm() { return protocol + ":" + (host.length() > 0 || protocol.equals("file") || protocol.equals("http") || protocol.equals("https") ? "//" + getAuthority() : "") + file + (ref != null ? "#" + ref : ""); }
    public String toString() { return toExternalForm(); }
    public URI toURI() throws URISyntaxException { return new URI(toExternalForm()); }
    public URLConnection openConnection() throws IOException {
        if (protocol.equals("http") || protocol.equals("https")) return new HttpURLConnection(this) {
            public void disconnect() {}
            public boolean usingProxy() { return false; }
        };
        return new URLConnection(this) { public void connect() {} };
    }
    public final InputStream openStream() throws IOException { return openConnection().getInputStream(); }
    public final Object getContent() throws IOException { return openStream(); }
    public boolean equals(Object o) { return o instanceof URL && o.toString().equals(toString()); }
    public int hashCode() { return toString().hashCode(); }
    public boolean sameFile(URL o) { return o != null && protocol.equals(o.protocol) && host.equals(o.host) && file.equals(o.file); }
}
