package java.awt.image;

import java.awt.Graphics;
import java.awt.Graphics2D;

/* Images are int pixels: TYPE_INT_RGB keeps 0xRRGGBB, TYPE_INT_ARGB 0xAARRGGBB.
 * Every other type is stored as ARGB. */
public class BufferedImage extends java.awt.Image implements RenderedImage {
    public static final int TYPE_CUSTOM = 0, TYPE_INT_RGB = 1, TYPE_INT_ARGB = 2, TYPE_INT_ARGB_PRE = 3, TYPE_INT_BGR = 4,
        TYPE_3BYTE_BGR = 5, TYPE_4BYTE_ABGR = 6, TYPE_BYTE_GRAY = 10, TYPE_BYTE_INDEXED = 13;
    private final int width, height, type;
    private final int[] pixels;
    private final boolean alpha;
    private WritableRaster raster;

    public BufferedImage(int width, int height, int type) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException("Width (" + width + ") and height (" + height + ") cannot be <= 0");
        this.width = width; this.height = height; this.type = type;
        pixels = new int[width * height];
        alpha = type != TYPE_INT_RGB && type != TYPE_INT_BGR && type != TYPE_3BYTE_BGR && type != TYPE_BYTE_GRAY;
    }
    /* Used by ImageIO: takes ownership of ARGB pixels. */
    public BufferedImage(int width, int height, int[] argb) {
        this.width = width; this.height = height; type = TYPE_INT_ARGB; pixels = argb; alpha = true;
    }
    public int getType() { return type; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public int getWidth(ImageObserver o) { return width; }
    public int getHeight(ImageObserver o) { return height; }
    public boolean hasAlpha() { return alpha; }
    public int getRGB(int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) throw new ArrayIndexOutOfBoundsException("Coordinate out of bounds!");
        int p = pixels[y * width + x];
        return alpha ? p : p | 0xff000000;
    }
    public void setRGB(int x, int y, int rgb) {
        if (x < 0 || y < 0 || x >= width || y >= height) throw new ArrayIndexOutOfBoundsException("Coordinate out of bounds!");
        pixels[y * width + x] = alpha ? rgb : rgb & 0xffffff;
    }
    public int[] getRGB(int x, int y, int w, int h, int[] out, int offset, int scan) {
        if (out == null) out = new int[offset + h * scan];
        for (int j = 0; j < h; j++) {
            int src = (y + j) * width + x, dst = offset + j * scan;
            if (alpha) System.arraycopy(pixels, src, out, dst, w);
            else for (int i = 0; i < w; i++) out[dst + i] = pixels[src + i] | 0xff000000;
        }
        return out;
    }
    public void setRGB(int x, int y, int w, int h, int[] in, int offset, int scan) {
        for (int j = 0; j < h; j++) {
            int dst = (y + j) * width + x, src = offset + j * scan;
            if (alpha) System.arraycopy(in, src, pixels, dst, w);
            else for (int i = 0; i < w; i++) pixels[dst + i] = in[src + i] & 0xffffff;
        }
    }
    public WritableRaster getRaster() {
        if (raster == null) raster = new WritableRaster(new DataBufferInt(pixels, pixels.length), width, height);
        return raster;
    }
    public Graphics getGraphics() { return createGraphics(); }
    public Graphics2D createGraphics() { return java.awt.ImageAccess.graphics(this); }
    public BufferedImage getSubimage(int x, int y, int w, int h) {
        BufferedImage r = new BufferedImage(w, h, alpha ? TYPE_INT_ARGB : TYPE_INT_RGB);
        r.setRGB(0, 0, w, h, getRGB(x, y, w, h, null, 0, w), 0, w);
        return r;
    }
    public java.awt.image.ColorModel getColorModel() { return new ColorModel(alpha); }
}
