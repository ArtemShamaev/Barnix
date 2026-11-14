package java.lang;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;

public final class Class<T> implements java.io.Serializable {
    private int vmClass;
    private String name;
    private Object[] enumCache;

    private Class() {}
    public String getName() { if (name == null) name = getName0(); return name; }
    private native String getName0();
    public static Class<?> forName(String name) throws ClassNotFoundException { return forName0(name, true); }
    public static Class<?> forName(String name, boolean initialize, ClassLoader loader) throws ClassNotFoundException { return forName0(name, initialize); }
    private static native Class<?> forName0(String name, boolean initialize);
    public native boolean isInstance(Object o);
    public native boolean isAssignableFrom(Class<?> other);
    public native boolean isInterface();
    public native boolean isArray();
    public native boolean isPrimitive();
    public native int getModifiers();
    public native Class<? super T> getSuperclass();
    public native Class<?> getComponentType();
    public native Class<?>[] getInterfaces();
    static native Class<?> getPrimitiveClass(String name);
    private native void initialize0();
    private static native byte[] getResourceBytes(String name);
    private native Object[] members0(boolean constructors, boolean publicOnly);
    private native Object[] enumConstants0();

    public boolean isEnum() { return (getModifiers() & 0x4000) != 0 && getSuperclass() == Enum.class; }
    public boolean isAnnotation() { return (getModifiers() & 0x2000) != 0; }
    public boolean isSynthetic() { return (getModifiers() & 0x1000) != 0; }
    public boolean isAnonymousClass() { return false; }
    public boolean desiredAssertionStatus() { return false; }
    public ClassLoader getClassLoader() { return isPrimitive() ? null : ClassLoader.getSystemClassLoader(); }
    public Package getPackage() { return null; }

    public String getSimpleName() {
        if (isArray()) return getComponentType().getSimpleName() + "[]";
        String n = getName();
        int i = Math.max(n.lastIndexOf('.'), n.lastIndexOf('$'));
        return n.substring(i + 1);
    }
    public String getCanonicalName() { return isArray() ? getComponentType().getCanonicalName() + "[]" : getName().replace('$', '.'); }
    public String getTypeName() { return isArray() ? getComponentType().getTypeName() + "[]" : getName(); }

    public String toString() { return (isInterface() ? "interface " : isPrimitive() ? "" : "class ") + getName(); }

    @SuppressWarnings("unchecked")
    public T cast(Object o) {
        if (o != null && !isInstance(o)) throw new ClassCastException("Cannot cast " + o.getClass().getName() + " to " + getName());
        return (T) o;
    }

    @SuppressWarnings("unchecked")
    public T newInstance() throws InstantiationException, IllegalAccessException {
        if (isInterface() || isArray() || isPrimitive() || (getModifiers() & 0x400) != 0) throw new InstantiationException(getName());
        Constructor<T> c;
        try { c = getDeclaredConstructor(); } catch (NoSuchMethodException e) { throw new InstantiationException(getName()); }
        try { return c.newInstance(); }
        catch (InvocationTargetException e) { Thread.throwUnchecked(e.getCause()); return null; }
    }

    private String resolveName(String n) {
        if (n == null) return null;
        if (n.startsWith("/")) return n.substring(1);
        Class<?> c = this;
        while (c.isArray()) c = c.getComponentType();
        String base = c.getName();
        int i = base.lastIndexOf('.');
        return i < 0 ? n : base.substring(0, i).replace('.', '/') + "/" + n;
    }
    public InputStream getResourceAsStream(String n) {
        byte[] b = getResourceBytes(resolveName(n));
        return b == null ? null : new ByteArrayInputStream(b);
    }
    public java.net.URL getResource(String n) { return ClassLoader.resourceUrl(resolveName(n)); }
    static byte[] resourceBytes(String n) { return getResourceBytes(n); }

    private static boolean sameTypes(Class<?>[] a, Class<?>[] b) {
        if (b == null) b = new Class<?>[0];
        if (a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    @SuppressWarnings("unchecked")
    public Constructor<T> getConstructor(Class<?>... types) throws NoSuchMethodException {
        for (Object o : members0(true, true)) { Constructor<T> c = (Constructor<T>) o; if (sameTypes(c.getParameterTypes(), types)) return c; }
        throw new NoSuchMethodException(getName() + ".<init>");
    }
    @SuppressWarnings("unchecked")
    public Constructor<T> getDeclaredConstructor(Class<?>... types) throws NoSuchMethodException {
        for (Object o : members0(true, false)) { Constructor<T> c = (Constructor<T>) o; if (sameTypes(c.getParameterTypes(), types)) return c; }
        throw new NoSuchMethodException(getName() + ".<init>");
    }
    public Constructor<?>[] getConstructors() { Object[] m = members0(true, true); Constructor<?>[] r = new Constructor<?>[m.length]; System.arraycopy(m, 0, r, 0, m.length); return r; }
    public Constructor<?>[] getDeclaredConstructors() { Object[] m = members0(true, false); Constructor<?>[] r = new Constructor<?>[m.length]; System.arraycopy(m, 0, r, 0, m.length); return r; }
    public Method getDeclaredMethod(String n, Class<?>... types) throws NoSuchMethodException {
        for (Object o : members0(false, false)) { Method m = (Method) o; if (m.getName().equals(n) && sameTypes(m.getParameterTypes(), types)) return m; }
        throw new NoSuchMethodException(getName() + "." + n);
    }
    public Method getMethod(String n, Class<?>... types) throws NoSuchMethodException {
        for (Class<?> c = this; c != null; c = c.getSuperclass())
            for (Object o : c.members0(false, true)) { Method m = (Method) o; if (m.getName().equals(n) && sameTypes(m.getParameterTypes(), types)) return m; }
        for (Class<?> i : getInterfaces()) { try { return i.getMethod(n, types); } catch (NoSuchMethodException e) {} }
        throw new NoSuchMethodException(getName() + "." + n);
    }
    public Method[] getDeclaredMethods() { Object[] m = members0(false, false); Method[] r = new Method[m.length]; System.arraycopy(m, 0, r, 0, m.length); return r; }
    public Method[] getMethods() {
        java.util.ArrayList<Method> list = new java.util.ArrayList<Method>();
        for (Class<?> c = this; c != null; c = c.getSuperclass()) for (Object o : c.members0(false, true)) list.add((Method) o);
        return list.toArray(new Method[list.size()]);
    }

    @SuppressWarnings("unchecked")
    public T[] getEnumConstants() {
        if (!isEnum()) return null;
        if (enumCache == null) enumCache = enumConstants0();
        return enumCache == null ? null : (T[]) enumCache.clone();
    }
    Object[] enumConstantsShared() { if (enumCache == null) enumCache = enumConstants0(); return enumCache; }
}
