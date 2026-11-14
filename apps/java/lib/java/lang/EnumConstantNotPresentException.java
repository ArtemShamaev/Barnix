package java.lang;

public class EnumConstantNotPresentException extends RuntimeException {
    public EnumConstantNotPresentException(Class<? extends Enum> type, String name) { super(type.getName() + "." + name); }
}
