package java.util;

public final class Locale implements java.io.Serializable, Cloneable {
    public static final Locale ENGLISH = new Locale("en"), US = new Locale("en", "US"), UK = new Locale("en", "GB"),
        ROOT = new Locale(""), GERMAN = new Locale("de"), FRENCH = new Locale("fr"), CHINA = new Locale("zh", "CN"),
        JAPAN = new Locale("ja", "JP"), GERMANY = new Locale("de", "DE"), FRANCE = new Locale("fr", "FR"), CANADA = new Locale("en", "CA");
    private static Locale defaultLocale = US;
    private final String language, country, variant;
    public Locale(String language) { this(language, "", ""); }
    public Locale(String language, String country) { this(language, country, ""); }
    public Locale(String language, String country, String variant) { this.language = language.toLowerCase(); this.country = country.toUpperCase(); this.variant = variant; }
    public static Locale getDefault() { return defaultLocale; }
    public static void setDefault(Locale l) { defaultLocale = l; }
    public static Locale[] getAvailableLocales() { return new Locale[] {US, ENGLISH}; }
    public String getLanguage() { return language; }
    public String getCountry() { return country; }
    public String getVariant() { return variant; }
    public String getDisplayName() { return toString(); }
    public String toString() { return country.length() == 0 ? language : language + "_" + country + (variant.length() == 0 ? "" : "_" + variant); }
    public boolean equals(Object o) { return o instanceof Locale && o.toString().equals(toString()); }
    public int hashCode() { return toString().hashCode(); }
}
