package java.awt;

import java.awt.event.*;
import java.util.ArrayList;

/* A window is the whole Barnix screen; it is displayable from the start. */
public class Window extends Container {
    private final ArrayList<WindowListener> listeners = new ArrayList<WindowListener>();
    public Window() { visible = false; }
    public void pack() {
        addNotify();
        Dimension d = getPreferredSize();
        setSize(d.width, d.height);
        validate();
    }
    public void setVisible(boolean v) {
        if (v && !displayable) pack();
        super.setVisible(v);
        if (v) for (WindowListener l : listeners) l.windowOpened(new WindowEvent(this, WindowEvent.WINDOW_OPENED));
    }
    public void dispose() {
        removeNotify();
        for (WindowListener l : new ArrayList<WindowListener>(listeners)) l.windowClosed(new WindowEvent(this, WindowEvent.WINDOW_CLOSED));
    }
    public void addWindowListener(WindowListener l) { if (l != null) listeners.add(l); }
    public void removeWindowListener(WindowListener l) { listeners.remove(l); }
    public void setLocationRelativeTo(Component c) {}
    public void toFront() {}
    public void toBack() {}
    public void setAlwaysOnTop(boolean b) {}
    public void setIconImage(Image i) {}
    public void setIconImages(java.util.List<? extends Image> i) {}
    public boolean isActive() { return true; }
    public boolean isFocused() { return true; }
}
