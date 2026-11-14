package org.w3c.dom;

public interface Document extends Node {
    Element getDocumentElement();
    NodeList getElementsByTagName(String name);
}
