package java.net;

public class InetSocketAddress extends SocketAddress {
    private final String host;
    private final InetAddress address;
    private final int port;
    public InetSocketAddress(String host, int port) { this.host = host; this.port = port; InetAddress a = null; try { a = InetAddress.getByName(host); } catch (UnknownHostException e) {} address = a; }
    public InetSocketAddress(InetAddress a, int port) { address = a; host = a.getHostName(); this.port = port; }
    public InetSocketAddress(int port) { this("localhost", port); }
    public static InetSocketAddress createUnresolved(String host, int port) { return new InetSocketAddress(host, port); }
    public final int getPort() { return port; }
    public final InetAddress getAddress() { return address; }
    public final String getHostName() { return host; }
    public final String getHostString() { return host; }
    public final boolean isUnresolved() { return address == null; }
    public String toString() { return (address != null ? address.toString() : host + "/<unresolved>") + ":" + port; }
}
