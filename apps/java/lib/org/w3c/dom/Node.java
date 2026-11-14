package org.w3c.dom;

public interface Node {
    short ELEMENT_NODE = 1, ATTRIBUTE_NODE = 2, TEXT_NODE = 3, CDATA_SECTION_NODE = 4, COMMENT_NODE = 8, DOCUMENT_NODE = 9;
    short getNodeType();
    String getNodeName();
    String getNodeValue();
    String getTextContent();
    Node getParentNode();
    NodeList getChildNodes();
    Node getFirstChild();
    boolean hasChildNodes();
}
