package java.awt;

public abstract class Image {
    public static final int SCALE_DEFAULT = 1, SCALE_FAST = 2, SCALE_SMOOTH = 4;
    public abstract int getWidth(java.awt.image.ImageObserver o);
    public abstract int getHeight(java.awt.image.ImageObserver o);
    public abstract Graphics getGraphics();
    public void flush() {}
}
