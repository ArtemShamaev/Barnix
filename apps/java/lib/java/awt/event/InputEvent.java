package java.awt.event;

public abstract class InputEvent extends ComponentEvent {
    public static final int SHIFT_MASK = 1, CTRL_MASK = 2, META_MASK = 4, ALT_MASK = 8, BUTTON1_MASK = 16, BUTTON2_MASK = 8, BUTTON3_MASK = 4;
    protected int modifiers;
    InputEvent(java.awt.Component source, int id, int modifiers) { super(source, id); this.modifiers = modifiers; }
    public int getModifiers() { return modifiers; }
    public boolean isShiftDown() { return (modifiers & SHIFT_MASK) != 0; }
    public boolean isControlDown() { return (modifiers & CTRL_MASK) != 0; }
    public boolean isAltDown() { return (modifiers & ALT_MASK) != 0; }
    public void consume() {}
}
