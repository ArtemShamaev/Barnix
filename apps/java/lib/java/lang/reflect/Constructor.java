package java.lang.reflect;

public final class Constructor<T> extends AccessibleObject implements Member {
    private Class<T> clazz;
    private int slot, modifiers;
    private Class<?>[] parameterTypes;

    private Constructor() {}
    public Class<T> getDeclaringClass() { return clazz; }
    public String getName() { return clazz.getName(); }
    public int getModifiers() { return modifiers; }
    public Class<?>[] getParameterTypes() { return parameterTypes.clone(); }
    public int getParameterCount() { return parameterTypes.length; }
    private native Object newInstance0(Object[] args);
    @SuppressWarnings("unchecked")
    public T newInstance(Object... args) throws InstantiationException, IllegalAccessException, IllegalArgumentException, InvocationTargetException {
        return (T) newInstance0(args);
    }
    public String toString() {
        StringBuilder b = new StringBuilder(Modifier.isPublic(modifiers) ? "public " : "").append(getName()).append('(');
        for (int i = 0; i < parameterTypes.length; i++) { if (i > 0) b.append(','); b.append(parameterTypes[i].getTypeName()); }
        return b.append(')').toString();
    }
}
