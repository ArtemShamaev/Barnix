package nova;

import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.jar.Attributes;
import java.util.jar.JarEntry;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;

/** Командная строка: nova build файл.nova [-o out.jar] | nova run файл.nova [аргументы...] */
public final class Main {
    static final String VERSION = "0.1.0";

    record Compiled(String mainClass, Map<String, byte[]> classes) {}

    public static void main(String[] args) throws Exception {
        if (args.length < 2 || !(args[0].equals("build") || args[0].equals("run") || args[0].equals("check"))) {
            usage();
            System.exit(args.length == 1 && (args[0].equals("--version") || args[0].equals("-v")) ? 0 : 2);
        }
        Path src = Path.of(args[1]);
        Compiled c = compileOrExit(src);
        switch (args[0]) {
            case "check" -> System.out.println("ok: " + src);
            case "build" -> {
                Path out = null;
                for (int i = 2; i < args.length; i++) {
                    if (args[i].equals("-o") && i + 1 < args.length) out = Path.of(args[++i]);
                }
                if (out == null) out = Path.of(baseName(src) + ".jar");
                writeJar(c, out);
                System.out.println("собрано: " + out + "  (запуск: java -jar " + out + ")");
            }
            case "run" -> run(c, Arrays.copyOfRange(args, 2, args.length));
            default -> usage();
        }
    }

    private static void usage() {
        System.err.println("""
                Nova %s — компилятор в байткод JVM
                использование:
                  nova build файл.nova [-o программа.jar]   собрать запускаемый .jar
                  nova run   файл.nova [аргументы...]       скомпилировать в памяти и запустить
                  nova check файл.nova                      только проверить типы""".formatted(VERSION));
    }

    static Compiled compileOrExit(Path src) throws IOException {
        String text = Files.readString(src);
        try {
            return compile(text, src.getFileName().toString());
        } catch (CompileError e) {
            report(src.toString(), text, e);
            System.exit(1);
            return null;
        }
    }

    static Compiled compile(String text, String fileName) {
        List<Token> tokens = new Lexer(text).run();
        Ast.Program prog = new Parser(tokens).parseProgram();
        Checker ck = new Checker();
        ck.check(prog);
        String mainClass = mainClassName(fileName);
        while (ck.classes.containsKey(mainClass)) mainClass += "Main";
        Map<String, byte[]> classes = new CodeGen(ck, mainClass, fileName).generate(prog);
        return new Compiled(mainClass, classes);
    }

    private static void report(String file, String text, CompileError e) {
        System.err.println(file + ":" + e.line + ":" + e.col + ": ошибка: " + e.getMessage());
        String[] lines = text.replace("\r\n", "\n").split("\n", -1);
        if (e.line >= 1 && e.line <= lines.length) {
            String l = lines[e.line - 1];
            System.err.println("    " + l);
            StringBuilder caret = new StringBuilder("    ");
            for (int i = 0; i < e.col - 1 && i < l.length(); i++) caret.append(l.charAt(i) == '\t' ? '\t' : ' ');
            System.err.println(caret.append('^'));
        }
    }

    private static String baseName(Path p) {
        String n = p.getFileName().toString();
        int dot = n.lastIndexOf('.');
        return dot > 0 ? n.substring(0, dot) : n;
    }

    private static String mainClassName(String fileName) {
        String b = baseName(Path.of(fileName));
        StringBuilder sb = new StringBuilder();
        boolean up = true;
        for (char ch : b.toCharArray()) {
            if (!Character.isLetterOrDigit(ch)) { up = true; continue; }
            sb.append(up ? Character.toUpperCase(ch) : ch);
            up = false;
        }
        if (sb.isEmpty() || !Character.isLetter(sb.charAt(0))) sb.insert(0, "App");
        return sb.toString();
    }

    static void writeJar(Compiled c, Path out) throws IOException {
        Manifest mf = new Manifest();
        mf.getMainAttributes().put(Attributes.Name.MANIFEST_VERSION, "1.0");
        mf.getMainAttributes().put(Attributes.Name.MAIN_CLASS, c.mainClass());
        mf.getMainAttributes().putValue("Created-By", "Nova " + VERSION);
        if (out.getParent() != null) Files.createDirectories(out.getParent());
        try (OutputStream fos = Files.newOutputStream(out); JarOutputStream jar = new JarOutputStream(fos, mf)) {
            for (Map.Entry<String, byte[]> e : c.classes().entrySet()) {
                jar.putNextEntry(new JarEntry(e.getKey() + ".class"));
                jar.write(e.getValue());
                jar.closeEntry();
            }
        }
    }

    private static final class BytesLoader extends ClassLoader {
        private final Map<String, byte[]> classes;
        BytesLoader(Map<String, byte[]> classes) { super(Main.class.getClassLoader()); this.classes = classes; }

        @Override
        protected Class<?> findClass(String name) throws ClassNotFoundException {
            byte[] b = classes.get(name.replace('.', '/'));
            if (b == null) throw new ClassNotFoundException(name);
            return defineClass(name, b, 0, b.length);
        }
    }

    private static void run(Compiled c, String[] args) throws Exception {
        Class<?> cls = new BytesLoader(c.classes()).loadClass(c.mainClass().replace('/', '.'));
        Method m = cls.getMethod("main", String[].class);
        try {
            m.invoke(null, (Object) args);
        } catch (InvocationTargetException e) {
            Throwable t = e.getCause();
            System.out.flush();
            System.err.println("ошибка выполнения: " + t);
            for (StackTraceElement el : t.getStackTrace()) {
                if (el.getClassName().startsWith("jdk.internal") || el.getClassName().startsWith("java.lang.reflect")) break;
                System.err.println("    at " + el);
            }
            System.exit(1);
        }
    }
}
