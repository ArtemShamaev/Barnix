package javax.sound.midi;

/* No MIDI devices on Barnix. */
public class MidiSystem {
    private MidiSystem() {}
    public static MidiDevice.Info[] getMidiDeviceInfo() { return new MidiDevice.Info[0]; }
    public static MidiDevice getMidiDevice(MidiDevice.Info info) throws MidiUnavailableException { throw new MidiUnavailableException("No MIDI devices"); }
    public static Receiver getReceiver() throws MidiUnavailableException { throw new MidiUnavailableException("No MIDI devices"); }
    public static Sequencer getSequencer() throws MidiUnavailableException { throw new MidiUnavailableException("No MIDI devices"); }
    public static Synthesizer getSynthesizer() throws MidiUnavailableException { throw new MidiUnavailableException("No MIDI devices"); }
    public static Sequence getSequence(java.net.URL url) throws InvalidMidiDataException, java.io.IOException { throw new InvalidMidiDataException("MIDI is not supported"); }
    public static Sequence getSequence(java.io.InputStream in) throws InvalidMidiDataException, java.io.IOException { throw new InvalidMidiDataException("MIDI is not supported"); }
}
