package javax.sound.sampled;

public class AudioFormat {
    private final float rate;
    private final int bits, channels;
    private final boolean signed, bigEndian;
    public AudioFormat(float sampleRate, int sampleSizeInBits, int channels, boolean signed, boolean bigEndian) {
        rate = sampleRate; bits = sampleSizeInBits; this.channels = channels; this.signed = signed; this.bigEndian = bigEndian;
    }
    public float getSampleRate() { return rate; }
    public int getSampleSizeInBits() { return bits; }
    public int getChannels() { return channels; }
    public int getFrameSize() { return (bits + 7) / 8 * channels; }
    public float getFrameRate() { return rate; }
    public boolean isBigEndian() { return bigEndian; }
    public String toString() { return rate + " Hz, " + bits + " bit, " + channels + " channels"; }
}
