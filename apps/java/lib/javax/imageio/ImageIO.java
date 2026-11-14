package javax.imageio;

import java.awt.image.BufferedImage;
import java.awt.image.RenderedImage;
import java.io.*;
import java.net.URL;

/* PNG only: decoding and encoding are native (natives_io.c). */
public final class ImageIO {
    private ImageIO() {}
    private static native int[] decode0(byte[] data);
    private static native byte[] encode0(int[] argb, int width, int height, boolean alpha);

    public static BufferedImage read(InputStream in) throws IOException {
        if (in == null) throw new IllegalArgumentException("input == null!");
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        for (int n; (n = in.read(b)) > 0; ) out.write(b, 0, n);
        int[] r = decode0(out.toByteArray());
        if (r == null) return null;
        int w = r[0], h = r[1];
        int[] px = new int[w * h];
        System.arraycopy(r, 2, px, 0, w * h);
        return new BufferedImage(w, h, px);
    }
    public static BufferedImage read(File f) throws IOException {
        if (f == null) throw new IllegalArgumentException("input == null!");
        if (!f.canRead()) throw new IIOException("Can't read input file!");
        InputStream in = new FileInputStream(f);
        try { return read(in); } finally { in.close(); }
    }
    public static BufferedImage read(URL u) throws IOException {
        if (u == null) throw new IllegalArgumentException("input == null!");
        InputStream in = u.openStream();
        try { return read(in); } finally { in.close(); }
    }
    public static boolean write(RenderedImage im, String format, OutputStream out) throws IOException {
        if (!"png".equalsIgnoreCase(format) || !(im instanceof BufferedImage)) return false;
        BufferedImage b = (BufferedImage) im;
        int w = b.getWidth(), h = b.getHeight();
        out.write(encode0(b.getRGB(0, 0, w, h, null, 0, w), w, h, b.hasAlpha()));
        return true;
    }
    public static boolean write(RenderedImage im, String format, File f) throws IOException {
        OutputStream out = new FileOutputStream(f);
        try { return write(im, format, out); } finally { out.close(); }
    }
    public static String[] getReaderFormatNames() { return new String[] {"png"}; }
    public static String[] getWriterFormatNames() { return new String[] {"png"}; }
    public static void setUseCache(boolean b) {}
}
