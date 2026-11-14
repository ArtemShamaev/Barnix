package java.util.zip;

public class Deflater {
    public static final int DEFLATED = 8, NO_COMPRESSION = 0, BEST_SPEED = 1, BEST_COMPRESSION = 9, DEFAULT_COMPRESSION = -1,
        FILTERED = 1, HUFFMAN_ONLY = 2, DEFAULT_STRATEGY = 0, NO_FLUSH = 0, SYNC_FLUSH = 2, FULL_FLUSH = 3;
    private final boolean nowrap;
    private final java.io.ByteArrayOutputStream input = new java.io.ByteArrayOutputStream();
    private byte[] output;
    private int outPos;
    private boolean finish, finished;
    private long totalIn, totalOut;

    static native byte[] deflate0(byte[] in, int off, int len);

    public Deflater() { this(DEFAULT_COMPRESSION, false); }
    public Deflater(int level) { this(level, false); }
    public Deflater(int level, boolean nowrap) { this.nowrap = nowrap; }
    public void setInput(byte[] b) { setInput(b, 0, b.length); }
    public void setInput(byte[] b, int off, int len) { input.write(b, off, len); totalIn += len; }
    public void setLevel(int l) {}
    public void setStrategy(int s) {}
    public void setDictionary(byte[] b) {}
    public boolean needsInput() { return !finish; }
    public void finish() { finish = true; }
    public boolean finished() { return finished; }
    public int deflate(byte[] b) { return deflate(b, 0, b.length); }
    public int deflate(byte[] b, int off, int len, int flush) { return deflate(b, off, len); }
    public int deflate(byte[] b, int off, int len) {
        if (!finish) return 0;
        if (output == null) {
            byte[] data = input.toByteArray();
            byte[] raw = deflate0(data, 0, data.length);
            if (nowrap) output = raw;
            else {
                Adler32 a = new Adler32();
                a.update(data, 0, data.length);
                long v = a.getValue();
                output = new byte[raw.length + 6];
                output[0] = 0x78; output[1] = 0x01;
                System.arraycopy(raw, 0, output, 2, raw.length);
                for (int i = 0; i < 4; i++) output[raw.length + 2 + i] = (byte) (v >> (24 - 8 * i));
            }
        }
        int n = Math.min(len, output.length - outPos);
        System.arraycopy(output, outPos, b, off, n);
        outPos += n;
        totalOut += n;
        if (outPos == output.length) finished = true;
        return n;
    }
    public int getAdler() { return 1; }
    public int getTotalIn() { return (int) totalIn; }
    public int getTotalOut() { return (int) totalOut; }
    public long getBytesRead() { return totalIn; }
    public long getBytesWritten() { return totalOut; }
    public void reset() { input.reset(); output = null; outPos = 0; finish = finished = false; totalIn = totalOut = 0; }
    public void end() {}
}
