package java.awt;

public class Frame extends Window {
    public static final int NORMAL = 0, ICONIFIED = 1, MAXIMIZED_BOTH = 6;
    private String title = "";
    public Frame() {}
    public Frame(String title) { setTitle(title); }
    public String getTitle() { return title; }
    public void setTitle(String t) { title = t == null ? "" : t; }
    public void setResizable(boolean r) {}
    public boolean isResizable() { return true; }
    public void setUndecorated(boolean u) {}
    public void setExtendedState(int s) {}
    public int getExtendedState() { return NORMAL; }
    public void setMenuBar(Object m) {}
}
