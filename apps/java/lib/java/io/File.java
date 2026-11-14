package java.io;

import java.util.ArrayList;

public class File implements Serializable, Comparable<File> {
    public static final char separatorChar = '/', pathSeparatorChar = ':';
    public static final String separator = "/", pathSeparator = ":";
    private final String path;

    private static native boolean exists0(String p);
    private static native boolean isDirectory0(String p);
    private static native long length0(String p);
    private static native String[] list0(String p);
    private static native boolean mkdir0(String p);
    private static native boolean delete0(String p);
    private static native boolean rename0(String a, String b);
    private static native String cwd0();

    private static String normalize(String p) {
        StringBuilder b = new StringBuilder(p.length());
        char last = 0;
        for (int i = 0; i < p.length(); i++) {
            char c = p.charAt(i);
            if (c == '\\') c = '/';
            if (c == '/' && last == '/') continue;
            b.append(c);
            last = c;
        }
        if (b.length() > 1 && b.charAt(b.length() - 1) == '/') b.setLength(b.length() - 1);
        return b.toString();
    }
    public File(String p) { if (p == null) throw new NullPointerException(); path = normalize(p); }
    public File(String parent, String child) {
        if (child == null) throw new NullPointerException();
        path = parent == null ? normalize(child) : normalize(parent.length() == 0 ? "/" + child : parent + "/" + child);
    }
    public File(File parent, String child) { this(parent == null ? null : parent.path, child); }
    public File(java.net.URI uri) { this(uri.getPath()); }

    public String getName() { int i = path.lastIndexOf('/'); return path.substring(i + 1); }
    public String getParent() {
        int i = path.lastIndexOf('/');
        if (i < 0) return null;
        if (i == 0) return path.length() > 1 ? "/" : null;
        return path.substring(0, i);
    }
    public File getParentFile() { String p = getParent(); return p == null ? null : new File(p); }
    public String getPath() { return path; }
    public boolean isAbsolute() { return path.startsWith("/"); }
    public String getAbsolutePath() {
        if (isAbsolute()) return path;
        String base = System.getProperty("user.dir");
        if (base == null) base = cwd0();
        return path.length() == 0 ? base : base.endsWith("/") ? base + path : base + "/" + path;
    }
    public File getAbsoluteFile() { return new File(getAbsolutePath()); }
    public String getCanonicalPath() throws IOException {
        String[] parts = getAbsolutePath().split("/");
        ArrayList<String> out = new ArrayList<String>();
        for (String s : parts) {
            if (s.length() == 0 || s.equals(".")) continue;
            if (s.equals("..")) { if (!out.isEmpty()) out.remove(out.size() - 1); continue; }
            out.add(s);
        }
        StringBuilder b = new StringBuilder();
        for (String s : out) b.append('/').append(s);
        return b.length() == 0 ? "/" : b.toString();
    }
    public File getCanonicalFile() throws IOException { return new File(getCanonicalPath()); }
    private String native_() { return path.length() == 0 ? "." : path; }
    public boolean exists() { return exists0(native_()); }
    public boolean isDirectory() { return isDirectory0(native_()); }
    public boolean isFile() { return exists() && !isDirectory(); }
    public boolean isHidden() { return getName().startsWith("."); }
    public boolean canRead() { return exists(); }
    public boolean canWrite() { return true; }
    public boolean canExecute() { return exists(); }
    public long lastModified() { return exists() ? 1 : 0; }
    public boolean setLastModified(long t) { return exists(); }
    public boolean setReadOnly() { return exists(); }
    public boolean setWritable(boolean w) { return exists(); }
    public boolean setExecutable(boolean x) { return exists(); }
    public long length() { return length0(native_()); }
    public boolean createNewFile() throws IOException {
        if (exists()) return false;
        if (!FileOutputStream.write0(native_(), null, 0, 0, false)) throw new IOException("Cannot create " + path);
        return true;
    }
    public boolean delete() { return delete0(native_()); }
    public void deleteOnExit() {}
    public String[] list() { return list0(native_()); }
    public String[] list(FilenameFilter f) {
        String[] all = list();
        if (all == null || f == null) return all;
        ArrayList<String> r = new ArrayList<String>();
        for (String s : all) if (f.accept(this, s)) r.add(s);
        return r.toArray(new String[r.size()]);
    }
    public File[] listFiles() {
        String[] n = list();
        if (n == null) return null;
        File[] r = new File[n.length];
        for (int i = 0; i < n.length; i++) r[i] = new File(this, n[i]);
        return r;
    }
    public File[] listFiles(FileFilter f) {
        File[] all = listFiles();
        if (all == null || f == null) return all;
        ArrayList<File> r = new ArrayList<File>();
        for (File x : all) if (f.accept(x)) r.add(x);
        return r.toArray(new File[r.size()]);
    }
    public File[] listFiles(FilenameFilter f) {
        String[] n = list(f);
        if (n == null) return null;
        File[] r = new File[n.length];
        for (int i = 0; i < n.length; i++) r[i] = new File(this, n[i]);
        return r;
    }
    public boolean mkdir() { return mkdir0(native_()); }
    public boolean mkdirs() {
        if (exists()) return false;
        if (mkdir()) return true;
        File p = getParentFile();
        return p != null && (p.mkdirs() || p.exists()) && mkdir();
    }
    public boolean renameTo(File dest) {
        if (dest.exists() && !dest.isDirectory()) dest.delete();
        return rename0(native_(), dest.native_());
    }
    public static File[] listRoots() { return new File[] {new File("/")}; }
    public long getTotalSpace() { return 64L << 20; }
    public long getFreeSpace() { return 32L << 20; }
    public long getUsableSpace() { return 32L << 20; }
    public static File createTempFile(String prefix, String suffix) throws IOException { return createTempFile(prefix, suffix, null); }
    public static File createTempFile(String prefix, String suffix, File dir) throws IOException {
        if (dir == null) dir = new File(System.getProperty("java.io.tmpdir", "/tmp"));
        dir.mkdirs();
        for (int i = 0; ; i++) {
            File f = new File(dir, prefix + (System.nanoTime() + i) % 1000000 + (suffix == null ? ".tmp" : suffix));
            if (f.createNewFile()) return f;
        }
    }
    public java.net.URI toURI() { try { return new java.net.URI("file://" + getAbsolutePath() + (isDirectory() ? "/" : "")); } catch (java.net.URISyntaxException e) { throw new RuntimeException(e); } }
    @Deprecated public java.net.URL toURL() throws java.net.MalformedURLException { return new java.net.URL("file://" + getAbsolutePath()); }
    public int compareTo(File o) { return path.compareTo(o.path); }
    public boolean equals(Object o) { return o instanceof File && ((File) o).path.equals(path); }
    public int hashCode() { return path.hashCode() ^ 1234321; }
    public String toString() { return path; }
}
