package java.awt.event;

public class ComponentEvent extends java.awt.AWTEvent {
    public ComponentEvent(java.awt.Component source, int id) { super(source, id); }
    public java.awt.Component getComponent() { return (java.awt.Component) getSource(); }
}
