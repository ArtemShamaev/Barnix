package java.awt;

public class Font {
    public static final int PLAIN = 0, BOLD = 1, ITALIC = 2;
    public static final String MONOSPACED = "Monospaced", SANS_SERIF = "SansSerif", SERIF = "Serif", DIALOG = "Dialog";
    protected String name;
    protected int style, size;
    public Font(String name, int style, int size) { this.name = name; this.style = style; this.size = size; }
    public String getName() { return name; }
    public String getFamily() { return name; }
    public int getStyle() { return style; }
    public int getSize() { return size; }
}
