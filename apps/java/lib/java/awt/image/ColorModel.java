package java.awt.image;

public class ColorModel {
    private final boolean alpha;
    ColorModel(boolean alpha) { this.alpha = alpha; }
    public boolean hasAlpha() { return alpha; }
}
