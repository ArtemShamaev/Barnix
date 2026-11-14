package java.net;

import java.io.*;

/* A TCP connection through the Barnix network stack. */
public class Socket implements Closeable {
    private int handle = -1;
    private InetSocketAddress remote;
    private int timeout;
    private boolean closed;
    private static native int open0(String host, int port);
    private static native int send0(int handle, byte[] b, int off, int len);
    private static native int recv0(int handle, byte[] b, int off, int len, int timeoutMs);
    private static native void close0(int handle);

    public Socket() {}
    public Socket(String host, int port) throws UnknownHostException, IOException { connect(new InetSocketAddress(host, port)); }
    public Socket(InetAddress a, int port) throws IOException { connect(new InetSocketAddress(a, port)); }
    public void connect(SocketAddress a) throws IOException { connect(a, 0); }
    public void connect(SocketAddress a, int timeoutMs) throws IOException {
        InetSocketAddress s = (InetSocketAddress) a;
        String host = s.getAddress() != null ? s.getAddress().getHostAddress() : s.getHostName();
        handle = open0(host, s.getPort());
        if (handle < 0) {
            if (handle == -2) throw new UnknownHostException(s.getHostName());
            throw new ConnectException("Connection refused: " + s.getHostName() + ":" + s.getPort());
        }
        remote = s;
    }
    public InputStream getInputStream() throws IOException {
        if (handle < 0 || closed) throw new SocketException("Socket is not connected");
        return new InputStream() {
            private final byte[] one = new byte[1];
            public int read() throws IOException { int n = read(one, 0, 1); return n <= 0 ? -1 : one[0] & 0xff; }
            public int read(byte[] b, int off, int len) throws IOException {
                if (len == 0) return 0;
                for (;;) {
                    int n = recv0(handle, b, off, len, timeout > 0 ? timeout : 250);
                    if (n == -2) { if (timeout > 0) throw new SocketTimeoutException("Read timed out"); if (closed) return -1; Thread.yield(); continue; }
                    if (n < 0) throw new SocketException("Connection reset");
                    return n == 0 ? -1 : n;
                }
            }
            public void close() throws IOException { Socket.this.close(); }
        };
    }
    public OutputStream getOutputStream() throws IOException {
        if (handle < 0 || closed) throw new SocketException("Socket is not connected");
        return new OutputStream() {
            public void write(int b) throws IOException { write(new byte[] {(byte) b}, 0, 1); }
            public void write(byte[] b, int off, int len) throws IOException { if (send0(handle, b, off, len) < 0) throw new SocketException("Broken pipe"); }
            public void close() throws IOException { Socket.this.close(); }
        };
    }
    public SocketAddress getRemoteSocketAddress() { return remote; }
    public InetAddress getInetAddress() { return remote == null ? null : remote.getAddress(); }
    public int getPort() { return remote == null ? 0 : remote.getPort(); }
    public boolean isConnected() { return handle >= 0; }
    public boolean isClosed() { return closed; }
    public void setSoTimeout(int ms) { timeout = ms; }
    public int getSoTimeout() { return timeout; }
    public void setTcpNoDelay(boolean b) {}
    public void setTrafficClass(int c) {}
    public void setKeepAlive(boolean b) {}
    public void setReceiveBufferSize(int n) {}
    public void setSendBufferSize(int n) {}
    public void shutdownInput() {}
    public void shutdownOutput() {}
    public synchronized void close() { if (!closed && handle >= 0) close0(handle); closed = true; }
}
