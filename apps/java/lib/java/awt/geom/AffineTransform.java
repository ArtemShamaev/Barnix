package java.awt.geom;

/* Scale and translation only. */
public class AffineTransform {
    private double sx = 1, shy, shx, sy = 1, tx, ty;
    public AffineTransform() {}
    public AffineTransform(double m00, double m10, double m01, double m11, double m02, double m12) { sx = m00; shy = m10; shx = m01; sy = m11; tx = m02; ty = m12; }
    public AffineTransform(AffineTransform t) { this(t.sx, t.shy, t.shx, t.sy, t.tx, t.ty); }
    public double getScaleX() { return sx; }
    public double getScaleY() { return sy; }
    public double getTranslateX() { return tx; }
    public double getTranslateY() { return ty; }
    public double getShearX() { return shx; }
    public double getShearY() { return shy; }
    public void translate(double x, double y) { tx += x * sx + y * shx; ty += x * shy + y * sy; }
    public void scale(double x, double y) { sx *= x; shy *= x; shx *= y; sy *= y; }
    public static AffineTransform getScaleInstance(double x, double y) { return new AffineTransform(x, 0, 0, y, 0, 0); }
    public static AffineTransform getTranslateInstance(double x, double y) { return new AffineTransform(1, 0, 0, 1, x, y); }
}
