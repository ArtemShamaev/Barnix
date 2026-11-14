package javax.sound.sampled;

/* WAV (PCM) parsing for codecs; there are no audio lines. */
public class AudioSystem {
    private AudioSystem() {}
    private static int u16(byte[] b, int o) { return (b[o] & 255) | (b[o + 1] & 255) << 8; }
    private static int u32(byte[] b, int o) { return u16(b, o) | u16(b, o + 2) << 16; }
    public static AudioInputStream getAudioInputStream(java.io.InputStream in) throws UnsupportedAudioFileException, java.io.IOException {
        java.io.DataInputStream d = new java.io.DataInputStream(in);
        byte[] h = new byte[12];
        d.readFully(h);
        if (h[0] != 'R' || h[1] != 'I' || h[2] != 'F' || h[3] != 'F' || h[8] != 'W' || h[9] != 'A' || h[10] != 'V' || h[11] != 'E')
            throw new UnsupportedAudioFileException("not a WAV file");
        AudioFormat format = null;
        byte[] c = new byte[8];
        for (;;) {
            d.readFully(c);
            int size = u32(c, 4);
            if (c[0] == 'f' && c[1] == 'm' && c[2] == 't') {
                byte[] f = new byte[size];
                d.readFully(f);
                if (u16(f, 0) != 1) throw new UnsupportedAudioFileException("compressed WAV");
                int bits = u16(f, 14);
                format = new AudioFormat(u32(f, 4), bits, u16(f, 2), bits > 8, false);
                if ((size & 1) != 0) d.skipBytes(1);
            } else if (c[0] == 'd' && c[1] == 'a' && c[2] == 't' && c[3] == 'a') {
                if (format == null) throw new UnsupportedAudioFileException("no fmt chunk");
                return new AudioInputStream(in, format, size / Math.max(1, format.getFrameSize()));
            } else d.skipBytes(size + (size & 1));
        }
    }
    public static AudioInputStream getAudioInputStream(java.net.URL url) throws UnsupportedAudioFileException, java.io.IOException {
        return getAudioInputStream(new java.io.BufferedInputStream(url.openStream()));
    }
}
