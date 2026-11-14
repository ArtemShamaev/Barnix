package javax.sound.midi;

public abstract class MidiMessage implements Cloneable {
    protected byte[] data;
    protected int length;
    protected MidiMessage(byte[] data) { this.data = data; length = data == null ? 0 : data.length; }
    public byte[] getMessage() { byte[] r = new byte[length]; if (data != null) System.arraycopy(data, 0, r, 0, length); return r; }
    public int getStatus() { return length > 0 ? data[0] & 255 : 0; }
    public int getLength() { return length; }
    public abstract Object clone();
}
