package java.net;

public class InetAddress implements java.io.Serializable {
    final String host;
    final int address;
    InetAddress(String host, int address) { this.host = host; this.address = address; }
    private static native int resolve0(String host);
    public static InetAddress getByName(String host) throws UnknownHostException {
        if (host == null || host.length() == 0 || host.equals("localhost")) return new InetAddress("localhost", 0x7f000001);
        int a = resolve0(host);
        if (a == 0) throw new UnknownHostException(host);
        return new InetAddress(host, a);
    }
    public static InetAddress[] getAllByName(String host) throws UnknownHostException { return new InetAddress[] {getByName(host)}; }
    public static InetAddress getLocalHost() { return new InetAddress("localhost", 0x7f000001); }
    public static InetAddress getLoopbackAddress() { return getLocalHost(); }
    public static InetAddress getByAddress(byte[] b) { return new InetAddress(null, (b[0] & 0xff) << 24 | (b[1] & 0xff) << 16 | (b[2] & 0xff) << 8 | (b[3] & 0xff)); }
    public String getHostName() { return host != null ? host : getHostAddress(); }
    public String getCanonicalHostName() { return getHostName(); }
    public String getHostAddress() { return (address >>> 24) + "." + (address >> 16 & 255) + "." + (address >> 8 & 255) + "." + (address & 255); }
    public byte[] getAddress() { return new byte[] {(byte) (address >> 24), (byte) (address >> 16), (byte) (address >> 8), (byte) address}; }
    public boolean isLoopbackAddress() { return (address >>> 24) == 127; }
    public boolean equals(Object o) { return o instanceof InetAddress && ((InetAddress) o).address == address; }
    public int hashCode() { return address; }
    public String toString() { return (host != null ? host : "") + "/" + getHostAddress(); }
}
