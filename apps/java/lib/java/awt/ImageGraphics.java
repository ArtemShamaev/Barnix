package java.awt;

import java.awt.geom.AffineTransform;
import java.awt.image.BufferedImage;
import java.awt.image.ImageObserver;

/* Software drawing into a BufferedImage: rectangles, lines and images,
 * with translation and integer-ish scaling. Text is not rendered. */
final class ImageGraphics extends Graphics2D {
    private final BufferedImage target;
    private Color color = Color.WHITE;
    private Font font = new Font("Dialog", Font.PLAIN, 12);
    private double sx = 1, sy = 1, tx, ty;
    private int clipX, clipY, clipW, clipH;

    ImageGraphics(BufferedImage target) {
        this.target = target;
        clipW = target.getWidth();
        clipH = target.getHeight();
    }
    public Graphics create() {
        ImageGraphics g = new ImageGraphics(target);
        g.color = color; g.font = font; g.sx = sx; g.sy = sy; g.tx = tx; g.ty = ty;
        g.clipX = clipX; g.clipY = clipY; g.clipW = clipW; g.clipH = clipH;
        return g;
    }
    public void dispose() {}
    public Color getColor() { return color; }
    public void setColor(Color c) { if (c != null) color = c; }
    public Font getFont() { return font; }
    public void setFont(Font f) { if (f != null) font = f; }
    public void translate(int x, int y) { tx += x * sx; ty += y * sy; }
    public void scale(double x, double y) { sx *= x; sy *= y; }
    public AffineTransform getTransform() { return new AffineTransform(sx, 0, 0, sy, tx, ty); }
    public void setTransform(AffineTransform t) { sx = t.getScaleX(); sy = t.getScaleY(); tx = t.getTranslateX(); ty = t.getTranslateY(); }
    public void setRenderingHint(RenderingHints.Key key, Object value) {}
    public Object getRenderingHint(RenderingHints.Key key) { return null; }

    private int dx(double x) { return (int) Math.floor(x * sx + tx); }
    private int dy(double y) { return (int) Math.floor(y * sy + ty); }
    private void blend(int x, int y, int argb) {
        if (x < clipX || y < clipY || x >= clipX + clipW || y >= clipY + clipH) return;
        if (x < 0 || y < 0 || x >= target.getWidth() || y >= target.getHeight()) return;
        int a = argb >>> 24;
        if (a == 0) return;
        if (a != 255) {
            int d = target.getRGB(x, y), da = d >>> 24;
            int oa = a + da * (255 - a) / 255;
            if (oa == 0) return;
            int r = (((argb >> 16) & 255) * a + ((d >> 16) & 255) * da * (255 - a) / 255) / oa;
            int g = (((argb >> 8) & 255) * a + ((d >> 8) & 255) * da * (255 - a) / 255) / oa;
            int b = ((argb & 255) * a + (d & 255) * da * (255 - a) / 255) / oa;
            argb = oa << 24 | r << 16 | g << 8 | b;
        }
        target.setRGB(x, y, argb);
    }
    private void fill(int x0, int y0, int x1, int y1, int argb) {
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) blend(x, y, argb);
    }
    public void fillRect(int x, int y, int w, int h) { fill(dx(x), dy(y), dx(x + w), dy(y + h), color.getRGB()); }
    public void clearRect(int x, int y, int w, int h) {
        int x0 = Math.max(dx(x), 0), y0 = Math.max(dy(y), 0), x1 = Math.min(dx(x + w), target.getWidth()), y1 = Math.min(dy(y + h), target.getHeight());
        for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) target.setRGB(px, py, 0);
    }
    public void drawLine(int x1, int y1, int x2, int y2) {
        int n = Math.max(Math.abs(x2 - x1), Math.abs(y2 - y1));
        for (int i = 0; i <= n; i++) {
            double f = n == 0 ? 0 : (double) i / n;
            fillRect((int) Math.round(x1 + (x2 - x1) * f), (int) Math.round(y1 + (y2 - y1) * f), 1, 1);
        }
    }
    public void drawString(String s, int x, int y) {}
    public boolean drawImage(Image img, int x, int y, ImageObserver o) {
        if (!(img instanceof BufferedImage)) return false;
        BufferedImage b = (BufferedImage) img;
        return drawImage(img, x, y, x + b.getWidth(), y + b.getHeight(), 0, 0, b.getWidth(), b.getHeight(), o);
    }
    public boolean drawImage(Image img, int x, int y, int w, int h, ImageObserver o) {
        if (!(img instanceof BufferedImage)) return false;
        BufferedImage b = (BufferedImage) img;
        return drawImage(img, x, y, x + w, y + h, 0, 0, b.getWidth(), b.getHeight(), o);
    }
    public boolean drawImage(Image img, int dx1, int dy1, int dx2, int dy2, int sx1, int sy1, int sx2, int sy2, ImageObserver o) {
        if (!(img instanceof BufferedImage)) return false;
        BufferedImage src = (BufferedImage) img;
        int x0 = dx(dx1), y0 = dy(dy1), x1 = dx(dx2), y1 = dy(dy2);
        if (x1 == x0 || y1 == y0) return true;
        for (int y = Math.min(y0, y1); y < Math.max(y0, y1); y++) {
            int py = sy1 + (int) ((y - y0 + 0.5) * (sy2 - sy1) / (y1 - y0));
            if (py < 0 || py >= src.getHeight()) continue;
            for (int x = Math.min(x0, x1); x < Math.max(x0, x1); x++) {
                int px = sx1 + (int) ((x - x0 + 0.5) * (sx2 - sx1) / (x1 - x0));
                if (px >= 0 && px < src.getWidth()) blend(x, y, src.getRGB(px, py));
            }
        }
        return true;
    }
    public void setClip(int x, int y, int w, int h) { clipX = dx(x); clipY = dy(y); clipW = dx(x + w) - clipX; clipH = dy(y + h) - clipY; }
    public void clipRect(int x, int y, int w, int h) {
        Rectangle r = new Rectangle(clipX, clipY, clipW, clipH).intersection(new Rectangle(dx(x), dy(y), dx(x + w) - dx(x), dy(y + h) - dy(y)));
        clipX = r.x; clipY = r.y; clipW = Math.max(0, r.width); clipH = Math.max(0, r.height);
    }
    public Rectangle getClipBounds() {
        int x = (int) Math.floor((clipX - tx) / sx), y = (int) Math.floor((clipY - ty) / sy);
        return new Rectangle(x, y, (int) Math.ceil(clipW / sx), (int) Math.ceil(clipH / sy));
    }
}
