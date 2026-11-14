package java.awt;

import java.awt.image.ImageObserver;

public abstract class Graphics {
    protected Graphics() {}
    public abstract Graphics create();
    public abstract void dispose();
    public abstract Color getColor();
    public abstract void setColor(Color c);
    public abstract Font getFont();
    public abstract void setFont(Font f);
    public FontMetrics getFontMetrics() { return getFontMetrics(getFont()); }
    public FontMetrics getFontMetrics(Font f) { return new FontMetrics(f); }
    public abstract void translate(int x, int y);
    public abstract void fillRect(int x, int y, int w, int h);
    public abstract void clearRect(int x, int y, int w, int h);
    public void drawRect(int x, int y, int w, int h) { fillRect(x, y, w + 1, 1); fillRect(x, y + h, w + 1, 1); fillRect(x, y, 1, h + 1); fillRect(x + w, y, 1, h + 1); }
    public abstract void drawLine(int x1, int y1, int x2, int y2);
    public abstract void drawString(String s, int x, int y);
    public abstract boolean drawImage(Image img, int x, int y, ImageObserver o);
    public abstract boolean drawImage(Image img, int x, int y, int w, int h, ImageObserver o);
    public boolean drawImage(Image img, int x, int y, Color bg, ImageObserver o) { return drawImage(img, x, y, o); }
    public abstract boolean drawImage(Image img, int dx1, int dy1, int dx2, int dy2, int sx1, int sy1, int sx2, int sy2, ImageObserver o);
    public abstract void setClip(int x, int y, int w, int h);
    public abstract Rectangle getClipBounds();
    public abstract void clipRect(int x, int y, int w, int h);
}
