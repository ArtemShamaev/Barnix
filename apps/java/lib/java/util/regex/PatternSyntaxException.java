package java.util.regex;

public class PatternSyntaxException extends IllegalArgumentException {
    private final String desc, pattern;
    private final int index;
    public PatternSyntaxException(String desc, String regex, int index) { super(desc + " near index " + index + "\n" + regex); this.desc = desc; pattern = regex; this.index = index; }
    public String getDescription() { return desc; }
    public String getPattern() { return pattern; }
    public int getIndex() { return index; }
}
