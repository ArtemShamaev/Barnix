package java.lang;

public class TypeNotPresentException extends RuntimeException {
    public TypeNotPresentException(String type, Throwable cause) { super("Type " + type + " not present", cause); }
}
