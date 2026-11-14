package java.lang;

public abstract class Enum<E extends Enum<E>> implements Comparable<E>, java.io.Serializable {
    private final String name;
    private final int ordinal;

    protected Enum(String name, int ordinal) { this.name = name; this.ordinal = ordinal; }
    public final String name() { return name; }
    public final int ordinal() { return ordinal; }
    public String toString() { return name; }
    public final boolean equals(Object o) { return this == o; }
    public final int hashCode() { return super.hashCode(); }
    protected final Object clone() throws CloneNotSupportedException { throw new CloneNotSupportedException(); }
    public final int compareTo(E o) { return ordinal - ((Enum<?>) o).ordinal; }
    @SuppressWarnings("unchecked")
    public final Class<E> getDeclaringClass() {
        Class<?> c = getClass(), s = c.getSuperclass();
        return (Class<E>) (s == Enum.class ? c : s);
    }
    public static <T extends Enum<T>> T valueOf(Class<T> type, String name) {
        if (name == null) throw new NullPointerException("Name is null");
        Object[] all = type.enumConstantsShared();
        if (all != null) for (Object o : all) if (((Enum<?>) o).name.equals(name)) return type.cast(o);
        throw new IllegalArgumentException("No enum constant " + type.getCanonicalName() + "." + name);
    }
}
