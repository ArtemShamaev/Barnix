package java.util.zip;

/* Decodes once all of a stream's input has arrived (no partial states). */
public class Inflater {
    private final boolean nowrap;
    private byte[] input = new byte[0];
    private int inputLength;
    private byte[] output;
    private int outPos, remaining;
    private boolean finished, ended;
    private long totalIn, totalOut;

    private static native byte[] inflate0(byte[] in, int off, int len, boolean raw);

    public Inflater() { this(false); }
    public Inflater(boolean nowrap) { this.nowrap = nowrap; }
    public void setInput(byte[] b) { setInput(b, 0, b.length); }
    public void setInput(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        if (inputLength + len > input.length) input = java.util.Arrays.copyOf(input, Math.max(input.length * 2, inputLength + len));
        System.arraycopy(b, off, input, inputLength, len);
        inputLength += len;
    }
    public void setDictionary(byte[] b) {}
    public boolean needsInput() { return output == null && !finished; }
    public boolean needsDictionary() { return false; }
    public boolean finished() { return finished; }
    public int getRemaining() { return output == null ? inputLength : remaining; }
    public int inflate(byte[] b) throws DataFormatException { return inflate(b, 0, b.length); }
    public int inflate(byte[] b, int off, int len) throws DataFormatException {
        if (ended) throw new NullPointerException("Inflater has been closed");
        if (output == null) {
            if (inputLength == 0) return 0;
            byte[] r = inflate0(input, 0, inputLength, nowrap);
            if (r == null) return 0;
            int used = (r[0] & 0xff) | (r[1] & 0xff) << 8 | (r[2] & 0xff) << 16 | (r[3] & 0xff) << 24;
            output = r;
            outPos = 4;
            remaining = inputLength - used;
            totalIn += used;
        }
        int n = Math.min(len, output.length - outPos);
        System.arraycopy(output, outPos, b, off, n);
        outPos += n;
        totalOut += n;
        if (outPos == output.length) finished = true;
        return n;
    }
    public int getAdler() { return 1; }
    public long getBytesRead() { return totalIn; }
    public long getBytesWritten() { return totalOut; }
    public int getTotalIn() { return (int) totalIn; }
    public int getTotalOut() { return (int) totalOut; }
    public void reset() { input = new byte[0]; inputLength = 0; output = null; outPos = 0; remaining = 0; finished = false; totalIn = totalOut = 0; }
    public void end() { ended = true; input = null; output = null; }
    /* the bytes left after the stream, for GZIP trailers */
    byte[] leftover() {
        byte[] r = new byte[remaining];
        System.arraycopy(input, inputLength - remaining, r, 0, remaining);
        return r;
    }
}
