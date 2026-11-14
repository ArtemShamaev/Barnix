package java.awt;

public class Cursor {
    public static final int DEFAULT_CURSOR = 0, CROSSHAIR_CURSOR = 1, TEXT_CURSOR = 2, WAIT_CURSOR = 3, HAND_CURSOR = 12;
    private final int type;
    public Cursor(int type) { this.type = type; }
    public static Cursor getPredefinedCursor(int type) { return new Cursor(type); }
    public static Cursor getDefaultCursor() { return new Cursor(DEFAULT_CURSOR); }
    public int getType() { return type; }
}
