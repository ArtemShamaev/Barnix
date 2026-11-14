package org.w3c.dom;

public interface Element extends Node {
    String getTagName();
    String getAttribute(String name);
    boolean hasAttribute(String name);
    NodeList getElementsByTagName(String name);
}
