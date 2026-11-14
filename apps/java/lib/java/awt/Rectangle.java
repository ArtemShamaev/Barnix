package java.awt;

public class Rectangle {
    public int x, y, width, height;
    public Rectangle() {}
    public Rectangle(int w, int h) { this(0, 0, w, h); }
    public Rectangle(int x, int y, int w, int h) { this.x = x; this.y = y; width = w; height = h; }
    public Rectangle intersection(Rectangle r) {
        int x0 = Math.max(x, r.x), y0 = Math.max(y, r.y);
        int x1 = Math.min(x + width, r.x + r.width), y1 = Math.min(y + height, r.y + r.height);
        return new Rectangle(x0, y0, x1 - x0, y1 - y0);
    }
    public boolean contains(int px, int py) { return px >= x && py >= y && px < x + width && py < y + height; }
    public String toString() { return "java.awt.Rectangle[x=" + x + ",y=" + y + ",width=" + width + ",height=" + height + "]"; }
}
