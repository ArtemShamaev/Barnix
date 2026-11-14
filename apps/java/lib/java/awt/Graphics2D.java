package java.awt;

import java.awt.geom.AffineTransform;

public abstract class Graphics2D extends Graphics {
    protected Graphics2D() {}
    public abstract void setRenderingHint(RenderingHints.Key key, Object value);
    public abstract Object getRenderingHint(RenderingHints.Key key);
    public abstract AffineTransform getTransform();
    public abstract void setTransform(AffineTransform t);
    public abstract void scale(double sx, double sy);
}
