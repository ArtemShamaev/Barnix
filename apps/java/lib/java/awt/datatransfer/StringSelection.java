package java.awt.datatransfer;

public class StringSelection implements Transferable, ClipboardOwner {
    private final String data;
    public StringSelection(String data) { this.data = data; }
    public DataFlavor[] getTransferDataFlavors() { return new DataFlavor[] {DataFlavor.stringFlavor}; }
    public boolean isDataFlavorSupported(DataFlavor f) { return DataFlavor.stringFlavor.equals(f); }
    public Object getTransferData(DataFlavor f) throws UnsupportedFlavorException {
        if (!isDataFlavorSupported(f)) throw new UnsupportedFlavorException(f);
        return data;
    }
    public void lostOwnership(Clipboard c, Transferable t) {}
}
