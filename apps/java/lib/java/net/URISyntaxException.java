package java.net;

public class URISyntaxException extends Exception {
    private final String input;
    public URISyntaxException(String input, String reason) { super(reason + ": " + input); this.input = input; }
    public String getInput() { return input; }
}
