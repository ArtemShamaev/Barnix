package java.awt;

public abstract class AWTEvent extends java.util.EventObject {
    protected int id;
    public AWTEvent(Object source, int id) { super(source); this.id = id; }
    public int getID() { return id; }
}
