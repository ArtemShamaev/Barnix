package javax.xml.parsers;

import java.io.*;
import java.util.ArrayList;
import java.util.HashMap;
import org.w3c.dom.*;
import org.xml.sax.SAXException;

/* A small non-validating XML parser: elements, attributes, text, CDATA,
 * the predefined and numeric entities. Comments, the prolog and DOCTYPE
 * are skipped. Input is UTF-8. */
public class DocumentBuilder {
    protected DocumentBuilder() {}

    public Document parse(File f) throws SAXException, IOException {
        InputStream in = new FileInputStream(f);
        try { return parse(in); } finally { in.close(); }
    }
    public Document parse(String uri) throws SAXException, IOException {
        InputStream in = new java.net.URL(uri).openStream();
        try { return parse(in); } finally { in.close(); }
    }
    public Document parse(InputStream in) throws SAXException, IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] b = new byte[4096];
        for (int n; (n = in.read(b)) > 0; ) out.write(b, 0, n);
        String s = new String(out.toByteArray(), "UTF-8");
        Doc doc = new Doc();
        new Parser(s).content(doc, null);
        if (doc.root == null) throw new SAXException("Premature end of file.");
        return doc;
    }

    private static final class Parser {
        private final String s;
        private int p;
        Parser(String s) { this.s = s; }
        private SAXException error(String what) { return new SAXException(what + " at offset " + p); }
        private boolean at(String t) { return s.startsWith(t, p); }
        private void skip(String end) throws SAXException {
            int i = s.indexOf(end, p);
            if (i < 0) throw error("Unterminated markup");
            p = i + end.length();
        }
        private String name() throws SAXException {
            int start = p;
            while (p < s.length()) {
                char c = s.charAt(p);
                if (Character.isWhitespace(c) || c == '>' || c == '/' || c == '=') break;
                p++;
            }
            if (p == start) throw error("Name expected");
            return s.substring(start, p);
        }
        private void space() { while (p < s.length() && Character.isWhitespace(s.charAt(p))) p++; }
        private String decode(String t) throws SAXException {
            if (t.indexOf('&') < 0) return t;
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < t.length(); i++) {
                char c = t.charAt(i);
                if (c != '&') { b.append(c); continue; }
                int e = t.indexOf(';', i);
                if (e < 0) throw error("Bad entity");
                String n = t.substring(i + 1, e);
                if (n.equals("lt")) b.append('<');
                else if (n.equals("gt")) b.append('>');
                else if (n.equals("amp")) b.append('&');
                else if (n.equals("quot")) b.append('"');
                else if (n.equals("apos")) b.append('\'');
                else if (n.startsWith("#x")) b.appendCodePoint(Integer.parseInt(n.substring(2), 16));
                else if (n.startsWith("#")) b.appendCodePoint(Integer.parseInt(n.substring(1)));
                else throw error("Unknown entity " + n);
                i = e;
            }
            return b.toString();
        }
        /* Parses children into parent (an element, or the document when it is null). */
        void content(Doc doc, El parent) throws SAXException {
            while (p < s.length()) {
                if (at("<!--")) skip("-->");
                else if (at("<?")) skip("?>");
                else if (at("<![CDATA[")) {
                    int start = p + 9;
                    skip("]]>");
                    if (parent != null) parent.add(new Txt(s.substring(start, p - 3)));
                } else if (at("<!")) skip(">");
                else if (at("</")) {
                    p += 2;
                    String n = name();
                    space();
                    if (parent == null || !n.equals(parent.name)) throw error("Unexpected </" + n + ">");
                    if (!at(">")) throw error("> expected");
                    p++;
                    return;
                } else if (at("<")) {
                    p++;
                    El e = new El(name());
                    for (;;) {
                        space();
                        if (at("/>")) { p += 2; break; }
                        if (at(">")) { p++; content(doc, e); break; }
                        String an = name();
                        space();
                        if (!at("=")) throw error("= expected");
                        p++;
                        space();
                        char q = p < s.length() ? s.charAt(p) : 0;
                        if (q != '"' && q != '\'') throw error("Quote expected");
                        int end = s.indexOf(q, p + 1);
                        if (end < 0) throw error("Unterminated attribute");
                        e.attributes.put(an, decode(s.substring(p + 1, end)));
                        p = end + 1;
                    }
                    if (parent != null) parent.add(e);
                    else if (doc.root == null) { doc.root = e; doc.add(e); }
                    else throw error("Content after the root element");
                } else {
                    int end = s.indexOf('<', p);
                    if (end < 0) end = s.length();
                    String t = s.substring(p, end);
                    p = end;
                    if (parent != null) parent.add(new Txt(decode(t)));
                    else if (t.trim().length() > 0) throw error("Text outside the root element");
                }
            }
            if (parent != null) throw error("Unterminated <" + parent.name + ">");
        }
    }

    private static class List implements NodeList {
        final ArrayList<Node> nodes = new ArrayList<Node>();
        public Node item(int i) { return i >= 0 && i < nodes.size() ? nodes.get(i) : null; }
        public int getLength() { return nodes.size(); }
    }
    private static abstract class N implements Node {
        Node parent;
        final List children = new List();
        void add(N n) { n.parent = this; children.nodes.add(n); }
        public String getNodeValue() { return null; }
        public Node getParentNode() { return parent; }
        public NodeList getChildNodes() { return children; }
        public Node getFirstChild() { return children.item(0); }
        public boolean hasChildNodes() { return children.getLength() > 0; }
        public String getTextContent() {
            StringBuilder b = new StringBuilder();
            for (Node n : children.nodes) b.append(n.getTextContent());
            return b.toString();
        }
        void find(String name, List out) {
            for (Node n : children.nodes)
                if (n instanceof El) {
                    if (name.equals("*") || name.equals(((El) n).name)) out.nodes.add(n);
                    ((El) n).find(name, out);
                }
        }
        public NodeList getElementsByTagName(String name) { List l = new List(); find(name, l); return l; }
    }
    private static final class Txt extends N implements Text {
        final String data;
        Txt(String data) { this.data = data; }
        public short getNodeType() { return TEXT_NODE; }
        public String getNodeName() { return "#text"; }
        public String getNodeValue() { return data; }
        public String getData() { return data; }
        public String getTextContent() { return data; }
    }
    private static final class El extends N implements Element {
        final String name;
        final HashMap<String, String> attributes = new HashMap<String, String>();
        El(String name) { this.name = name; }
        public short getNodeType() { return ELEMENT_NODE; }
        public String getNodeName() { return name; }
        public String getTagName() { return name; }
        public String getAttribute(String n) { String v = attributes.get(n); return v == null ? "" : v; }
        public boolean hasAttribute(String n) { return attributes.containsKey(n); }
    }
    private static final class Doc extends N implements Document {
        El root;
        public short getNodeType() { return DOCUMENT_NODE; }
        public String getNodeName() { return "#document"; }
        public String getTextContent() { return null; }
        public Element getDocumentElement() { return root; }
    }
}
