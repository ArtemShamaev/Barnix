package java.awt.datatransfer;

public interface ClipboardOwner {
    void lostOwnership(Clipboard c, Transferable t);
}
