package java.awt.datatransfer;

/* Clipboard shared by the program only. */
public class Clipboard {
    private final String name;
    private Transferable contents;
    public Clipboard(String name) { this.name = name; }
    public String getName() { return name; }
    public synchronized Transferable getContents(Object requestor) { return contents; }
    public synchronized void setContents(Transferable t, ClipboardOwner owner) { contents = t; }
}
