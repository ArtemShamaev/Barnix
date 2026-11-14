package java.awt;

import java.awt.event.*;

/* Barnix has no windowing toolkit: components only keep their state. */
public abstract class Component implements java.awt.image.ImageObserver {
    int x, y, width, height;
    Dimension preferred, minimum;
    Container parent;
    boolean visible = true, focusable = true, displayable;
    Font font;
    Color background, foreground;
    String name;

    protected Component() {}
    public int getX() { return x; }
    public int getY() { return y; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public Dimension getSize() { return new Dimension(width, height); }
    public Rectangle getBounds() { return new Rectangle(x, y, width, height); }
    public void setSize(int w, int h) { width = w; height = h; }
    public void setSize(Dimension d) { setSize(d.width, d.height); }
    public void setBounds(int x, int y, int w, int h) { this.x = x; this.y = y; setSize(w, h); }
    public void setLocation(int x, int y) { this.x = x; this.y = y; }
    public Dimension getPreferredSize() { return preferred != null ? new Dimension(preferred) : getSize(); }
    public void setPreferredSize(Dimension d) { preferred = d == null ? null : new Dimension(d); }
    public Dimension getMinimumSize() { return minimum != null ? new Dimension(minimum) : new Dimension(0, 0); }
    public void setMinimumSize(Dimension d) { minimum = d; }
    public Container getParent() { return parent; }
    public boolean isVisible() { return visible; }
    public void setVisible(boolean v) { visible = v; }
    public boolean isShowing() { return visible && displayable; }
    public boolean isDisplayable() { return displayable; }
    public void addNotify() { displayable = true; }
    public void removeNotify() { displayable = false; }
    public void setFocusable(boolean f) { focusable = f; }
    public boolean isFocusable() { return focusable; }
    public void requestFocus() {}
    public boolean requestFocusInWindow() { return true; }
    public boolean hasFocus() { return true; }
    public boolean isFocusOwner() { return true; }
    public void setFont(Font f) { font = f; }
    public Font getFont() { return font; }
    public void setBackground(Color c) { background = c; }
    public Color getBackground() { return background; }
    public void setForeground(Color c) { foreground = c; }
    public Color getForeground() { return foreground; }
    public void setName(String n) { name = n; }
    public String getName() { return name; }
    public void setEnabled(boolean e) {}
    public boolean isEnabled() { return true; }
    public void setIgnoreRepaint(boolean i) {}
    public Graphics getGraphics() { return null; }
    public FontMetrics getFontMetrics(Font f) { return new FontMetrics(f); }
    public void paint(Graphics g) {}
    public void update(Graphics g) { paint(g); }
    public void repaint() {}
    public void repaint(long tm, int x, int y, int w, int h) {}
    public void invalidate() {}
    public void validate() {}
    public void revalidate() {}
    public void doLayout() {}
    public Image createImage(int w, int h) { return new java.awt.image.BufferedImage(w, h, java.awt.image.BufferedImage.TYPE_INT_RGB); }
    public void setCursor(Cursor c) {}
    public Toolkit getToolkit() { return Toolkit.getDefaultToolkit(); }
    public void addKeyListener(KeyListener l) {}
    public void removeKeyListener(KeyListener l) {}
    public void addMouseListener(MouseListener l) {}
    public void removeMouseListener(MouseListener l) {}
    public void addMouseMotionListener(MouseMotionListener l) {}
    public void removeMouseMotionListener(MouseMotionListener l) {}
    public void addMouseWheelListener(MouseWheelListener l) {}
    public void addFocusListener(FocusListener l) {}
    public void addComponentListener(ComponentListener l) {}
    public boolean imageUpdate(Image img, int flags, int x, int y, int w, int h) { return false; }
}
