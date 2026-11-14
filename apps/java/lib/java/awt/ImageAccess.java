package java.awt;

/* Lets java.awt.image reach the package-private image graphics. */
public final class ImageAccess {
    private ImageAccess() {}
    public static Graphics2D graphics(java.awt.image.BufferedImage image) { return new ImageGraphics(image); }
}
