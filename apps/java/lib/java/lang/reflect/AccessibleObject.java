package java.lang.reflect;

public class AccessibleObject {
    private boolean accessible;
    protected AccessibleObject() {}
    public void setAccessible(boolean flag) { accessible = flag; }
    public boolean isAccessible() { return accessible; }
}
