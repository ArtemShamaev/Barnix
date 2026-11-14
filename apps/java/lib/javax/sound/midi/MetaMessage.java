package javax.sound.midi;

public class MetaMessage extends MidiMessage {
    private int type;
    public MetaMessage() { super(new byte[] {(byte) 0xFF, 0}); }
    public int getType() { return type; }
    public byte[] getData() { return new byte[0]; }
    public Object clone() { MetaMessage m = new MetaMessage(); m.type = type; return m; }
}
