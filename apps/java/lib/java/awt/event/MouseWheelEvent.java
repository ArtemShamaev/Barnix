package java.awt.event;

public class MouseWheelEvent extends MouseEvent {
    private final int rotation;
    public MouseWheelEvent(java.awt.Component source, int id, long when, int modifiers, int x, int y, int clickCount, boolean popup, int type, int amount, int rotation) {
        super(source, id, when, modifiers, x, y, clickCount, popup, NOBUTTON); this.rotation = rotation;
    }
    public int getWheelRotation() { return rotation; }
}
