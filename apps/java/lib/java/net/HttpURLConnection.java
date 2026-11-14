package java.net;

import java.io.*;

/* HTTP/1.0 over the Barnix TCP stack (plain http only). */
public abstract class HttpURLConnection extends URLConnection {
    public static final int HTTP_OK = 200, HTTP_CREATED = 201, HTTP_NO_CONTENT = 204, HTTP_MOVED_PERM = 301, HTTP_MOVED_TEMP = 302,
        HTTP_NOT_MODIFIED = 304, HTTP_BAD_REQUEST = 400, HTTP_UNAUTHORIZED = 401, HTTP_FORBIDDEN = 403, HTTP_NOT_FOUND = 404,
        HTTP_INTERNAL_ERROR = 500, HTTP_UNAVAILABLE = 503;
    protected String method = "GET";
    protected int responseCode = -1;
    protected String responseMessage;
    protected boolean instanceFollowRedirects = true;
    private byte[] body;
    private final java.util.LinkedHashMap<String, String> headers = new java.util.LinkedHashMap<String, String>();
    private ByteArrayOutputStream output;
    private static boolean followRedirects = true;

    protected HttpURLConnection(URL url) { super(url); }
    public static void setFollowRedirects(boolean b) { followRedirects = b; }
    public static boolean getFollowRedirects() { return followRedirects; }
    public void setInstanceFollowRedirects(boolean b) { instanceFollowRedirects = b; }
    public void setRequestMethod(String m) throws ProtocolException { method = m; }
    public String getRequestMethod() { return method; }
    public abstract void disconnect();
    public abstract boolean usingProxy();
    public OutputStream getOutputStream() throws IOException { if (output == null) output = new ByteArrayOutputStream(); doOutput = true; return output; }
    public void connect() throws IOException {
        if (connected) return;
        URL u = url;
        for (int hops = 0; ; hops++) {
            if (!u.getProtocol().equals("http")) throw new IOException("Only plain http is supported: " + u);
            int port = u.getPort() < 0 ? 80 : u.getPort();
            Socket s = new Socket(u.getHost(), port);
            try {
                StringBuilder req = new StringBuilder();
                String path = u.getFile().length() == 0 ? "/" : u.getFile();
                req.append(output != null && method.equals("GET") ? "POST" : method).append(' ').append(path).append(" HTTP/1.0\r\nHost: ").append(u.getHost()).append("\r\nConnection: close\r\n");
                for (java.util.Map.Entry<String, String> e : requestProperties().entrySet()) req.append(e.getKey()).append(": ").append(e.getValue()).append("\r\n");
                byte[] payload = output != null ? output.toByteArray() : null;
                if (payload != null) req.append("Content-Length: ").append(payload.length).append("\r\n");
                req.append("\r\n");
                OutputStream o = s.getOutputStream();
                o.write(req.toString().getBytes("ISO-8859-1"));
                if (payload != null) o.write(payload);
                o.flush();
                InputStream in = s.getInputStream();
                ByteArrayOutputStream all = new ByteArrayOutputStream();
                byte[] b = new byte[8192];
                int n;
                while ((n = in.read(b)) > 0) all.write(b, 0, n);
                byte[] r = all.toByteArray();
                int end = -1;
                for (int i = 0; i + 3 < r.length; i++) if (r[i] == '\r' && r[i + 1] == '\n' && r[i + 2] == '\r' && r[i + 3] == '\n') { end = i; break; }
                if (end < 0) throw new IOException("Malformed HTTP response");
                String[] lines = new String(r, 0, end, "ISO-8859-1").split("\r\n");
                String[] status = lines[0].split(" ", 3);
                responseCode = Integer.parseInt(status[1]);
                responseMessage = status.length > 2 ? status[2] : "";
                headers.clear();
                for (int i = 1; i < lines.length; i++) { int c = lines[i].indexOf(':'); if (c > 0) headers.put(lines[i].substring(0, c).trim().toLowerCase(), lines[i].substring(c + 1).trim()); }
                body = java.util.Arrays.copyOfRange(r, end + 4, r.length);
            } finally { s.close(); }
            String loc = headers.get("location");
            if (instanceFollowRedirects && followRedirects && loc != null && (responseCode == 301 || responseCode == 302 || responseCode == 303 || responseCode == 307) && hops < 5) { u = new URL(u, loc); continue; }
            break;
        }
        connected = true;
    }
    public int getResponseCode() throws IOException { connect(); return responseCode; }
    public String getResponseMessage() throws IOException { connect(); return responseMessage; }
    public InputStream getInputStream() throws IOException {
        connect();
        if (responseCode >= 400) throw new FileNotFoundException(url.toString());
        return new ByteArrayInputStream(body);
    }
    public InputStream getErrorStream() { return responseCode >= 400 && body != null ? new ByteArrayInputStream(body) : null; }
    public String getHeaderField(String name) { try { connect(); } catch (IOException e) { return null; } return headers.get(name.toLowerCase()); }
    public int getContentLength() { String v = getHeaderField("content-length"); return v == null ? -1 : Integer.parseInt(v); }
    public String getContentType() { return getHeaderField("content-type"); }
}
