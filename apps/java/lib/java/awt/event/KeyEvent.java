package java.awt.event;

public class KeyEvent extends InputEvent {
    public static final int KEY_TYPED = 400, KEY_PRESSED = 401, KEY_RELEASED = 402;
    public static final int VK_ENTER = 10, VK_ESCAPE = 27, VK_SPACE = 32;
    public static final char CHAR_UNDEFINED = 0xffff;
    private final int keyCode;
    private final char keyChar;
    public KeyEvent(java.awt.Component source, int id, long when, int modifiers, int keyCode, char keyChar) {
        super(source, id, modifiers); this.keyCode = keyCode; this.keyChar = keyChar;
    }
    public int getKeyCode() { return keyCode; }
    public char getKeyChar() { return keyChar; }
}
