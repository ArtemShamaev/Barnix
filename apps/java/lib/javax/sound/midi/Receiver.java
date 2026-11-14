package javax.sound.midi;

public interface Receiver extends AutoCloseable {
    void send(MidiMessage message, long timeStamp);
    void close();
}
