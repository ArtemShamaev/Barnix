package java.awt.datatransfer;

public interface Transferable {
    DataFlavor[] getTransferDataFlavors();
    boolean isDataFlavorSupported(DataFlavor f);
    Object getTransferData(DataFlavor f) throws UnsupportedFlavorException, java.io.IOException;
}
