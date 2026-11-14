package java.nio.charset;

public final class StandardCharsets {
    private StandardCharsets() {}
    public static final Charset UTF_8 = Charset.make("UTF-8"), ISO_8859_1 = Charset.make("ISO-8859-1"), US_ASCII = Charset.make("US-ASCII"),
        UTF_16 = Charset.make("UTF-16"), UTF_16BE = Charset.make("UTF-16BE"), UTF_16LE = Charset.make("UTF-16LE");
}
