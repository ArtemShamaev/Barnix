package java.net;

import java.io.*;

public abstract class URLConnection {
    protected URL url;
    protected boolean doInput = true, doOutput, useCaches, connected, allowUserInteraction;
    protected int connectTimeout, readTimeout;
    private final java.util.HashMap<String, String> request = new java.util.HashMap<String, String>();
    protected URLConnection(URL url) { this.url = url; }
    public abstract void connect() throws IOException;
    public URL getURL() { return url; }
    public InputStream getInputStream() throws IOException {
        String p = url.getProtocol();
        if (p.equals("file")) return new FileInputStream(url.getPath());
        if (p.equals("classpath")) {
            String path = url.getPath();
            InputStream in = ClassLoader.getSystemResourceAsStream(path.startsWith("/") ? path.substring(1) : path);
            if (in == null) throw new FileNotFoundException(url.toString());
            return in;
        }
        if (p.equals("jar")) {
            String s = url.toString();
            int bang = s.indexOf("!/");
            InputStream in = bang < 0 ? null : ClassLoader.getSystemResourceAsStream(s.substring(bang + 2));
            if (in == null) throw new FileNotFoundException(s);
            return in;
        }
        throw new UnknownServiceException("protocol doesn't support input");
    }
    public OutputStream getOutputStream() throws IOException { throw new UnknownServiceException("protocol doesn't support output"); }
    public int getContentLength() { return -1; }
    public long getContentLengthLong() { return -1; }
    public String getContentType() { return null; }
    public String getHeaderField(String name) { return null; }
    public String getHeaderField(int n) { return null; }
    public java.util.Map<String, java.util.List<String>> getHeaderFields() { return java.util.Collections.emptyMap(); }
    public long getLastModified() { return 0; }
    public void setDoInput(boolean b) { doInput = b; }
    public boolean getDoInput() { return doInput; }
    public void setDoOutput(boolean b) { doOutput = b; }
    public boolean getDoOutput() { return doOutput; }
    public void setUseCaches(boolean b) { useCaches = b; }
    public void setAllowUserInteraction(boolean b) { allowUserInteraction = b; }
    public void setConnectTimeout(int ms) { connectTimeout = ms; }
    public int getConnectTimeout() { return connectTimeout; }
    public void setReadTimeout(int ms) { readTimeout = ms; }
    public int getReadTimeout() { return readTimeout; }
    public void setRequestProperty(String k, String v) { request.put(k, v); }
    public void addRequestProperty(String k, String v) { request.put(k, v); }
    public String getRequestProperty(String k) { return request.get(k); }
    java.util.Map<String, String> requestProperties() { return request; }
}
