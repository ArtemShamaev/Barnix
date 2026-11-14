package java.awt;

public class Dimension {
    public int width, height;
    public Dimension() {}
    public Dimension(int w, int h) { width = w; height = h; }
    public Dimension(Dimension d) { this(d.width, d.height); }
    public double getWidth() { return width; }
    public double getHeight() { return height; }
    public void setSize(int w, int h) { width = w; height = h; }
    public boolean equals(Object o) { return o instanceof Dimension && ((Dimension) o).width == width && ((Dimension) o).height == height; }
    public int hashCode() { return width * 31 + height; }
    public String toString() { return "java.awt.Dimension[width=" + width + ",height=" + height + "]"; }
}
