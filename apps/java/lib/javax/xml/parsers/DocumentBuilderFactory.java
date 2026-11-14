package javax.xml.parsers;

public class DocumentBuilderFactory {
    protected DocumentBuilderFactory() {}
    public static DocumentBuilderFactory newInstance() { return new DocumentBuilderFactory(); }
    public DocumentBuilder newDocumentBuilder() throws ParserConfigurationException { return new DocumentBuilder(); }
    public void setNamespaceAware(boolean b) {}
    public void setValidating(boolean b) {}
    public void setIgnoringComments(boolean b) {}
    public void setIgnoringElementContentWhitespace(boolean b) {}
}
