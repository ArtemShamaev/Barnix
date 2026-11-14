package java.awt.datatransfer;

public class UnsupportedFlavorException extends Exception {
    public UnsupportedFlavorException(DataFlavor f) { super(f != null ? f.getHumanPresentableName() : null); }
}
