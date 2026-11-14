package javax.sound.midi;

public interface MidiDevice extends AutoCloseable {
    class Info {
        private final String name, vendor, description, version;
        protected Info(String name, String vendor, String description, String version) { this.name = name; this.vendor = vendor; this.description = description; this.version = version; }
        public final String getName() { return name; }
        public final String getVendor() { return vendor; }
        public final String getDescription() { return description; }
        public final String getVersion() { return version; }
        public final String toString() { return name; }
    }
    Info getDeviceInfo();
    void open() throws MidiUnavailableException;
    void close();
    boolean isOpen();
    Receiver getReceiver() throws MidiUnavailableException;
    Transmitter getTransmitter() throws MidiUnavailableException;
}
