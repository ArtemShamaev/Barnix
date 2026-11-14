package nova;

import java.util.Objects;

/** Тип в языке Nova. */
final class Type {
    enum K { INT, DOUBLE, BOOL, STRING, VOID, NULL, CLASS, ARRAY }

    final K k;
    final String cls;  // для CLASS
    final Type elem;   // для ARRAY

    private Type(K k, String cls, Type elem) {
        this.k = k;
        this.cls = cls;
        this.elem = elem;
    }

    static final Type INT = new Type(K.INT, null, null);
    static final Type DOUBLE = new Type(K.DOUBLE, null, null);
    static final Type BOOL = new Type(K.BOOL, null, null);
    static final Type STRING = new Type(K.STRING, null, null);
    static final Type VOID = new Type(K.VOID, null, null);
    static final Type NULL = new Type(K.NULL, null, null);

    static Type cls(String name) { return new Type(K.CLASS, name, null); }
    static Type arr(Type elem) { return new Type(K.ARRAY, null, elem); }

    boolean isNum() { return k == K.INT || k == K.DOUBLE; }
    boolean isRef() { return k == K.STRING || k == K.NULL || k == K.CLASS || k == K.ARRAY; }
    boolean is(K kind) { return k == kind; }

    String desc() {
        return switch (k) {
            case INT -> "I";
            case DOUBLE -> "D";
            case BOOL -> "Z";
            case VOID -> "V";
            case STRING -> "Ljava/lang/String;";
            case NULL -> "Ljava/lang/Object;";
            case CLASS -> "L" + cls + ";";
            case ARRAY -> "[" + elem.desc();
        };
    }

    /** Имя для NEW / ANEWARRAY / CHECKCAST. */
    String internal() {
        return switch (k) {
            case STRING -> "java/lang/String";
            case CLASS -> cls;
            case NULL -> "java/lang/Object";
            default -> desc();
        };
    }

    org.objectweb.asm.Type asm() { return org.objectweb.asm.Type.getType(desc()); }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof Type t)) return false;
        return k == t.k && Objects.equals(cls, t.cls) && Objects.equals(elem, t.elem);
    }

    @Override
    public int hashCode() { return Objects.hash(k, cls, elem); }

    @Override
    public String toString() {
        return switch (k) {
            case INT -> "int";
            case DOUBLE -> "double";
            case BOOL -> "bool";
            case STRING -> "string";
            case VOID -> "void";
            case NULL -> "null";
            case CLASS -> cls;
            case ARRAY -> elem + "[]";
        };
    }
}
