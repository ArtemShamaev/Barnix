package java.awt.image;

public class Raster {
    protected final DataBuffer buffer;
    protected final int width, height;
    Raster(DataBuffer buffer, int width, int height) { this.buffer = buffer; this.width = width; this.height = height; }
    public DataBuffer getDataBuffer() { return buffer; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
}
