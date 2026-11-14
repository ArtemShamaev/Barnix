package java.awt.image;

public final class DataBufferInt extends DataBuffer {
    private final int[] data;
    public DataBufferInt(int size) { this(new int[size], size); }
    public DataBufferInt(int[] data, int size) { super(size); this.data = data; }
    public int[] getData() { return data; }
    public int getElem(int i) { return data[i]; }
    public void setElem(int i, int v) { data[i] = v; }
}
