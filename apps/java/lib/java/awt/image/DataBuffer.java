package java.awt.image;

public abstract class DataBuffer {
    public static final int TYPE_BYTE = 0, TYPE_INT = 3;
    protected int size;
    protected DataBuffer(int size) { this.size = size; }
    public int getSize() { return size; }
    public abstract int getElem(int i);
    public abstract void setElem(int i, int v);
}
