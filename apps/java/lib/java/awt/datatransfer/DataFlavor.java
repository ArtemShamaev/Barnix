package java.awt.datatransfer;

public class DataFlavor {
    public static final DataFlavor stringFlavor = new DataFlavor("java.lang.String", "Unicode String");
    private final String type, name;
    public DataFlavor(String type, String name) { this.type = type; this.name = name; }
    public String getHumanPresentableName() { return name; }
    public String getMimeType() { return type; }
    public boolean equals(DataFlavor f) { return f != null && f.type.equals(type); }
    public boolean equals(Object o) { return o instanceof DataFlavor && equals((DataFlavor) o); }
    public int hashCode() { return type.hashCode(); }
}
