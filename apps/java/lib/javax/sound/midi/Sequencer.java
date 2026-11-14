package javax.sound.midi;

public interface Sequencer extends MidiDevice {
    void setSequence(Sequence s) throws InvalidMidiDataException;
    void start();
    void stop();
    boolean isRunning();
    void setMicrosecondPosition(long us);
    boolean addMetaEventListener(MetaEventListener l);
    void removeMetaEventListener(MetaEventListener l);
}
