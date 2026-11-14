package java.lang;

import java.io.InputStream;
import java.net.URL;

public abstract class ClassLoader {
    private static ClassLoader system;
    private final ClassLoader parent;

    protected ClassLoader() { this(null); }
    protected ClassLoader(ClassLoader parent) { this.parent = parent; }
    public static ClassLoader getSystemClassLoader() {
        if (system == null) system = new ClassLoader() {};
        return system;
    }
    public final ClassLoader getParent() { return parent; }
    public Class<?> loadClass(String name) throws ClassNotFoundException { return Class.forName(name, false, this); }
    protected Class<?> findClass(String name) throws ClassNotFoundException { throw new ClassNotFoundException(name); }
    public URL getResource(String name) { return resourceUrl(name.startsWith("/") ? name.substring(1) : name); }
    public InputStream getResourceAsStream(String name) {
        byte[] b = Class.resourceBytes(name.startsWith("/") ? name.substring(1) : name);
        return b == null ? null : new java.io.ByteArrayInputStream(b);
    }
    public static URL getSystemResource(String name) { return getSystemClassLoader().getResource(name); }
    public static InputStream getSystemResourceAsStream(String name) { return getSystemClassLoader().getResourceAsStream(name); }
    static URL resourceUrl(String name) {
        if (name == null || Class.resourceBytes(name) == null) return null;
        try { return new URL("classpath:/" + name); } catch (java.net.MalformedURLException e) { return null; }
    }
}
