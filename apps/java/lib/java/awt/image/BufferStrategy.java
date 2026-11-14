package java.awt.image;

/* Nothing on Barnix shows AWT drawing: the graphics go to an off-screen image. */
public class BufferStrategy {
    private BufferedImage image;
    public BufferStrategy() {}
    public java.awt.Graphics getDrawGraphics() {
        if (image == null) image = new BufferedImage(1, 1, BufferedImage.TYPE_INT_RGB);
        return image.getGraphics();
    }
    public void show() {}
    public boolean contentsLost() { return false; }
    public boolean contentsRestored() { return false; }
    public void dispose() {}
}
