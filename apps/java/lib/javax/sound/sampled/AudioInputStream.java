package javax.sound.sampled;

public class AudioInputStream extends java.io.InputStream {
    private final java.io.InputStream in;
    private final AudioFormat format;
    private final long frames;
    public AudioInputStream(java.io.InputStream in, AudioFormat format, long frames) { this.in = in; this.format = format; this.frames = frames; }
    public AudioFormat getFormat() { return format; }
    public long getFrameLength() { return frames; }
    public int read() throws java.io.IOException { return in.read(); }
    public int read(byte[] b, int off, int len) throws java.io.IOException { return in.read(b, off, len); }
    public int available() throws java.io.IOException { return in.available(); }
    public void close() throws java.io.IOException { in.close(); }
}
