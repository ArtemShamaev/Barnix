package java.awt;

/* Shown text goes to the console (crash reports use a TextArea). */
public class TextArea extends Component {
    public static final int SCROLLBARS_BOTH = 0, SCROLLBARS_VERTICAL_ONLY = 1, SCROLLBARS_HORIZONTAL_ONLY = 2, SCROLLBARS_NONE = 3;
    private String text;
    public TextArea() { this("", 0, 0, SCROLLBARS_BOTH); }
    public TextArea(String text) { this(text, 0, 0, SCROLLBARS_BOTH); }
    public TextArea(String text, int rows, int columns, int scrollbars) { this.text = text == null ? "" : text; }
    public String getText() { return text; }
    public void setText(String t) { text = t == null ? "" : t; }
    public void append(String t) { text += t; }
    public void setEditable(boolean e) {}
    public void addNotify() { super.addNotify(); System.err.println(text); }
}
