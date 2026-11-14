package java.awt.event;

public class MouseEvent extends InputEvent {
    public static final int MOUSE_CLICKED = 500, MOUSE_PRESSED = 501, MOUSE_RELEASED = 502, MOUSE_MOVED = 503, MOUSE_DRAGGED = 506;
    public static final int NOBUTTON = 0, BUTTON1 = 1, BUTTON2 = 2, BUTTON3 = 3;
    private final int x, y, clickCount, button;
    public MouseEvent(java.awt.Component source, int id, long when, int modifiers, int x, int y, int clickCount, boolean popup, int button) {
        super(source, id, modifiers); this.x = x; this.y = y; this.clickCount = clickCount; this.button = button;
    }
    public int getX() { return x; }
    public int getY() { return y; }
    public int getClickCount() { return clickCount; }
    public int getButton() { return button; }
}
