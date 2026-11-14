package java.awt.event;

public class WindowEvent extends ComponentEvent {
    public static final int WINDOW_OPENED = 200, WINDOW_CLOSING = 201, WINDOW_CLOSED = 202;
    public WindowEvent(java.awt.Window source, int id) { super(source, id); }
    public java.awt.Window getWindow() { return (java.awt.Window) getSource(); }
}
