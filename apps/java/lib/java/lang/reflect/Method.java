package java.lang.reflect;

public final class Method extends AccessibleObject implements Member {
    private Class<?> clazz;
    private String name;
    private int slot, modifiers;
    private Class<?>[] parameterTypes;
    private Class<?> returnType;

    private Method() {}
    public Class<?> getDeclaringClass() { return clazz; }
    public String getName() { return name; }
    public int getModifiers() { return modifiers; }
    public Class<?>[] getParameterTypes() { return parameterTypes.clone(); }
    public int getParameterCount() { return parameterTypes.length; }
    public Class<?> getReturnType() { return returnType; }
    private native Object invoke0(Object receiver, Object[] args);
    public Object invoke(Object receiver, Object... args) throws IllegalAccessException, IllegalArgumentException, InvocationTargetException {
        return invoke0(receiver, args);
    }
    public String toString() {
        StringBuilder b = new StringBuilder(Modifier.isPublic(modifiers) ? "public " : "");
        if (Modifier.isStatic(modifiers)) b.append("static ");
        b.append(returnType.getTypeName()).append(' ').append(clazz.getName()).append('.').append(name).append('(');
        for (int i = 0; i < parameterTypes.length; i++) { if (i > 0) b.append(','); b.append(parameterTypes[i].getTypeName()); }
        return b.append(')').toString();
    }
}
