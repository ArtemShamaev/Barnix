package java.awt;

public class Label extends Component {
    private String text;
    public Label() { this(""); }
    public Label(String text) { this.text = text; }
    public Label(String text, int alignment) { this.text = text; }
    public String getText() { return text; }
    public void setText(String t) { text = t; }
}
