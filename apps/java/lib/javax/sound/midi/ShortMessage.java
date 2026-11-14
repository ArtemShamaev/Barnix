package javax.sound.midi;

public class ShortMessage extends MidiMessage {
    public static final int CONTROL_CHANGE = 0xB0, PROGRAM_CHANGE = 0xC0, NOTE_ON = 0x90, NOTE_OFF = 0x80;
    public ShortMessage() { super(new byte[] {(byte) 0x90, 64, 127}); }
    public void setMessage(int command, int channel, int data1, int data2) throws InvalidMidiDataException {
        data = new byte[] {(byte) ((command & 0xF0) | (channel & 0x0F)), (byte) data1, (byte) data2}; length = 3;
    }
    public void setMessage(int status, int data1, int data2) throws InvalidMidiDataException { data = new byte[] {(byte) status, (byte) data1, (byte) data2}; length = 3; }
    public int getChannel() { return getStatus() & 0x0F; }
    public int getCommand() { return getStatus() & 0xF0; }
    public int getData1() { return length > 1 ? data[1] & 255 : 0; }
    public int getData2() { return length > 2 ? data[2] & 255 : 0; }
    public Object clone() { ShortMessage m = new ShortMessage(); m.data = getMessage(); m.length = length; return m; }
}
