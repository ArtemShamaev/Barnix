package java.awt;

/* Text is not drawn by AWT on Barnix; widths assume an 8x8 cell font. */
public class FontMetrics {
    protected Font font;
    protected FontMetrics(Font f) { font = f; }
    public Font getFont() { return font; }
    private int cell() { return font != null && font.getSize() > 0 ? (font.getSize() + 1) / 2 + 1 : 8; }
    public int stringWidth(String s) { return s == null ? 0 : s.length() * cell(); }
    public int charWidth(char c) { return cell(); }
    public int getHeight() { return font != null ? font.getSize() + 2 : 10; }
    public int getAscent() { return font != null ? font.getSize() : 8; }
    public int getDescent() { return 2; }
}
