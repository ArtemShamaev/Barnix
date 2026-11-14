package javax.sound.midi;

public interface Transmitter extends AutoCloseable {
    void setReceiver(Receiver r);
    Receiver getReceiver();
    void close();
}
