package java.awt;

import java.util.ArrayList;

public class Container extends Component {
    private final ArrayList<Component> children = new ArrayList<Component>();
    private LayoutManager layout;

    public Component add(Component c) { add(c, (Object) null); return c; }
    public Component add(String constraint, Component c) { add(c, (Object) constraint); return c; }
    public void add(Component c, Object constraints) {
        if (c.parent != null) c.parent.remove(c);
        children.add(c);
        c.parent = this;
        if (displayable) c.addNotify();
    }
    public void remove(Component c) {
        if (children.remove(c)) { if (c.displayable) c.removeNotify(); c.parent = null; }
    }
    public void removeAll() { while (!children.isEmpty()) remove(children.get(children.size() - 1)); }
    public int getComponentCount() { return children.size(); }
    public Component getComponent(int i) { return children.get(i); }
    public Component[] getComponents() { return children.toArray(new Component[children.size()]); }
    public void setLayout(LayoutManager l) { layout = l; }
    public LayoutManager getLayout() { return layout; }
    public Insets getInsets() { return new Insets(0, 0, 0, 0); }
    public void addNotify() { super.addNotify(); for (Component c : children) c.addNotify(); }
    public void removeNotify() { for (Component c : children) c.removeNotify(); super.removeNotify(); }
    /* Every child fills the container (BorderLayout CENTER is the only use). */
    public void doLayout() { for (Component c : children) c.setBounds(0, 0, width, height); }
    public void validate() { doLayout(); for (Component c : children) c.validate(); }
    public Dimension getPreferredSize() {
        if (preferred != null) return new Dimension(preferred);
        int w = 0, h = 0;
        for (Component c : children) { Dimension d = c.getPreferredSize(); w = Math.max(w, d.width); h = Math.max(h, d.height); }
        return new Dimension(w, h);
    }
    public void setSize(int w, int h) { super.setSize(w, h); doLayout(); }
}
