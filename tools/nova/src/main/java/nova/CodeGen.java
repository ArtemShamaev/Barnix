package nova;

import static nova.Ast.*;
import static org.objectweb.asm.Opcodes.*;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;

/** AST (уже проверенный Checker'ом) -> байткод JVM через ObjectWeb ASM. */
final class CodeGen {
    private static final int VERSION = V1_8;
    private static final String STRING = "java/lang/String";

    private static final String BUILDER = "java/lang/StringBuilder";

    private final Checker ck;
    private final String mainClass;
    private final String sourceFile;
    private final Map<String, String> superOf = new HashMap<>();

    private MethodVisitor mv;
    private Type curRet;
    private final Deque<Label[]> loops = new ArrayDeque<>(); // {break, continue}
    private final List<Object[]> debugLocals = new ArrayList<>(); // {Local, Label start}
    private String thisDesc;

    CodeGen(Checker ck, String mainClass, String sourceFile) {
        this.ck = ck;
        this.mainClass = mainClass;
        this.sourceFile = sourceFile;
        for (Checker.ClassInfo ci : ck.classes.values()) superOf.put(ci.name, ci.superInternal());
    }

    /** ClassWriter, который знает иерархию наших классов (нужно для COMPUTE_FRAMES). */
    private final class Writer extends ClassWriter {
        Writer() { super(COMPUTE_FRAMES); }

        private String parent(String c) {
            if (superOf.containsKey(c)) return superOf.get(c);
            return c.equals("java/lang/Object") ? null : "java/lang/Object";
        }

        @Override
        protected String getCommonSuperClass(String a, String b) {
            List<String> chainA = new ArrayList<>();
            for (String x = a; x != null; x = parent(x)) chainA.add(x);
            for (String x = b; x != null; x = parent(x)) if (chainA.contains(x)) return x;
            return "java/lang/Object";
        }
    }

    Map<String, byte[]> generate(Program prog) {
        Map<String, byte[]> out = new LinkedHashMap<>();
        for (ClassDecl cd : prog.classes) out.put(cd.name, genClass(ck.classes.get(cd.name)));
        out.put(mainClass, genMain(prog));
        return out;
    }

    // ================= классы =================

    private byte[] genClass(Checker.ClassInfo ci) {
        Writer cw = new Writer();
        cw.visit(VERSION, ACC_PUBLIC | ACC_SUPER, ci.name, null, ci.superInternal(), null);
        cw.visitSource(sourceFile, null);
        for (Checker.FieldInfo f : ci.fields.values())
            cw.visitField(ACC_PUBLIC, f.name, f.type.desc(), null, null).visitEnd();

        // конструктор
        FuncDecl init = ci.decl.init;
        String desc = ci.init == null ? "()V" : ci.init.desc();
        mv = cw.visitMethod(ACC_PUBLIC, "<init>", desc, null, null);
        mv.visitCode();
        curRet = Type.VOID;
        beginMethod("L" + ci.name + ";", init == null ? List.of() : init.params);
        lineNumber(init != null ? init : ci.decl);
        List<Stmt> body = init == null ? List.of() : init.body;
        mv.visitVarInsn(ALOAD, 0);
        int start = 0;
        if (!body.isEmpty() && body.get(0) instanceof SuperCall sc) {
            for (int i = 0; i < sc.args.size(); i++) exprAs(sc.args.get(i), sc.ctor.params.get(i));
            mv.visitMethodInsn(INVOKESPECIAL, ci.superInternal(), "<init>", sc.ctor == null ? "()V" : sc.ctor.desc(), false);
            start = 1;
        } else {
            mv.visitMethodInsn(INVOKESPECIAL, ci.superInternal(), "<init>", "()V", false);
        }
        for (FieldDecl fd : ci.decl.fields) {
            if (fd.init == null) continue;
            Checker.FieldInfo f = ci.fields.get(fd.name);
            lineNumber(fd);
            mv.visitVarInsn(ALOAD, 0);
            exprAs(fd.init, f.type);
            mv.visitFieldInsn(PUTFIELD, ci.name, f.name, f.type.desc());
        }
        block(body.subList(start, body.size()));
        mv.visitInsn(RETURN);
        endMethod();

        for (FuncDecl f : ci.decl.methods) genFunc(cw, f, ACC_PUBLIC);
        cw.visitEnd();
        return cw.toByteArray();
    }

    private void genFunc(ClassWriter cw, FuncDecl f, int access) {
        Checker.MethodInfo m = f.info;
        mv = cw.visitMethod(access, f.name, m.desc(), null, null);
        mv.visitCode();
        curRet = m.ret;
        beginMethod((access & ACC_STATIC) != 0 ? null : "L" + m.owner.name + ";", f.params);
        block(f.body);
        if (m.ret.is(Type.K.VOID)) mv.visitInsn(RETURN);
        endMethod();
    }

    private byte[] genMain(Program prog) {
        Writer cw = new Writer();
        cw.visit(VERSION, ACC_PUBLIC | ACC_SUPER | ACC_FINAL, mainClass, null, "java/lang/Object", null);
        cw.visitSource(sourceFile, null);
        cw.visitField(ACC_PRIVATE | ACC_STATIC, "$in", "Ljava/io/BufferedReader;", null, null).visitEnd();

        for (FuncDecl f : prog.funcs) genFunc(cw, f, ACC_PUBLIC | ACC_STATIC);

        if (ck.mainFunc.params.isEmpty()) {
            mv = cw.visitMethod(ACC_PUBLIC | ACC_STATIC, "main", "([Ljava/lang/String;)V", null, null);
            mv.visitCode();
            mv.visitMethodInsn(INVOKESTATIC, mainClass, "main", "()V", false);
            mv.visitInsn(RETURN);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }

        // static String $input(): лениво создаёт BufferedReader(System.in)
        mv = cw.visitMethod(ACC_PRIVATE | ACC_STATIC, "$input", "()Ljava/lang/String;", null, null);
        mv.visitCode();
        Label ready = new Label();
        mv.visitFieldInsn(GETSTATIC, mainClass, "$in", "Ljava/io/BufferedReader;");
        mv.visitJumpInsn(IFNONNULL, ready);
        mv.visitTypeInsn(NEW, "java/io/BufferedReader");
        mv.visitInsn(DUP);
        mv.visitTypeInsn(NEW, "java/io/InputStreamReader");
        mv.visitInsn(DUP);
        mv.visitFieldInsn(GETSTATIC, "java/lang/System", "in", "Ljava/io/InputStream;");
        mv.visitMethodInsn(INVOKESPECIAL, "java/io/InputStreamReader", "<init>", "(Ljava/io/InputStream;)V", false);
        mv.visitMethodInsn(INVOKESPECIAL, "java/io/BufferedReader", "<init>", "(Ljava/io/Reader;)V", false);
        mv.visitFieldInsn(PUTSTATIC, mainClass, "$in", "Ljava/io/BufferedReader;");
        mv.visitLabel(ready);
        mv.visitFieldInsn(GETSTATIC, mainClass, "$in", "Ljava/io/BufferedReader;");
        mv.visitMethodInsn(INVOKEVIRTUAL, "java/io/BufferedReader", "readLine", "()Ljava/lang/String;", false);
        mv.visitInsn(ARETURN);
        mv.visitMaxs(0, 0);
        mv.visitEnd();

        cw.visitEnd();
        return cw.toByteArray();
    }

    // ================= операторы =================

    private void beginMethod(String thisDescriptor, List<Param> params) {
        debugLocals.clear();
        thisDesc = thisDescriptor;
        Label start = new Label();
        mv.visitLabel(start);
        for (Param p : params) debugLocals.add(new Object[]{p.local, start});
        if (thisDesc != null) debugLocals.add(new Object[]{null, start});
    }

    /** Таблица локальных переменных: имена видны в стектрейсах и отладчике. */
    private void endMethod() {
        Label end = new Label();
        mv.visitLabel(end);
        for (Object[] d : debugLocals) {
            Checker.Local l = (Checker.Local) d[0];
            if (l == null) mv.visitLocalVariable("this", thisDesc, null, (Label) d[1], end, 0);
            else mv.visitLocalVariable(l.name, l.type.desc(), null, (Label) d[1], end, l.slot);
        }
        mv.visitMaxs(0, 0);
        mv.visitEnd();
    }

    private void declared(Checker.Local l) {
        Label start = new Label();
        mv.visitLabel(start);
        debugLocals.add(new Object[]{l, start});
    }

    private void lineNumber(Node n) {
        Label l = new Label();
        mv.visitLabel(l);
        mv.visitLineNumber(n.line, l);
    }

    private void block(List<Stmt> stmts) {
        for (Stmt s : stmts) {
            stmt(s);
            if (s instanceof Return || s instanceof Break || s instanceof Continue) break; // дальше недостижимо
        }
    }

    private void load(Checker.Local l) { mv.visitVarInsn(l.type.asm().getOpcode(ILOAD), l.slot); }
    private void store(Checker.Local l) { mv.visitVarInsn(l.type.asm().getOpcode(ISTORE), l.slot); }

    private void stmt(Stmt s) {
        lineNumber(s);
        switch (s) {
            case VarDecl d -> {
                if (d.init != null) exprAs(d.init, d.local.type);
                else pushDefault(d.local.type);
                store(d.local);
                declared(d.local);
            }
            case Assign a -> assign(a);
            case If i -> {
                Label elseL = new Label(), end = new Label();
                jump(i.cond, false, elseL);
                block(i.then);
                if (i.els != null) {
                    mv.visitJumpInsn(GOTO, end);
                    mv.visitLabel(elseL);
                    block(i.els);
                    mv.visitLabel(end);
                } else {
                    mv.visitLabel(elseL);
                }
            }
            case While w -> {
                Label start = new Label(), end = new Label();
                mv.visitLabel(start);
                jump(w.cond, false, end);
                loops.push(new Label[]{end, start});
                block(w.body);
                loops.pop();
                mv.visitJumpInsn(GOTO, start);
                mv.visitLabel(end);
            }
            case For f -> forStmt(f);
            case Return r -> {
                if (r.value == null) mv.visitInsn(RETURN);
                else {
                    exprAs(r.value, curRet);
                    mv.visitInsn(curRet.asm().getOpcode(IRETURN));
                }
            }
            case Break b -> mv.visitJumpInsn(GOTO, loops.peek()[0]);
            case Continue c -> mv.visitJumpInsn(GOTO, loops.peek()[1]);
            case Pass p -> {}
            case ExprStmt es -> {
                expr(es.expr);
                pop(es.expr.type);
            }
            case SuperCall sc -> throw new IllegalStateException("super(...) не в начале init");
            default -> throw new IllegalStateException("неизвестный оператор " + s);
        }
    }

    private void forStmt(For f) {
        Label start = new Label(), cont = new Label(), end = new Label();
        if (f.to != null) {
            exprAs(f.from, Type.INT);
            store(f.local);
            declared(f.local);
            exprAs(f.to, Type.INT);
            store(f.hidden1);
            mv.visitLabel(start);
            load(f.local);
            load(f.hidden1);
            mv.visitJumpInsn(IF_ICMPGE, end);
            loops.push(new Label[]{end, cont});
            block(f.body);
            loops.pop();
            mv.visitLabel(cont);
            mv.visitIincInsn(f.local.slot, 1);
        } else {
            expr(f.from);
            store(f.hidden1);
            mv.visitInsn(ICONST_0);
            store(f.hidden2);
            mv.visitLabel(start);
            load(f.hidden2);
            load(f.hidden1);
            mv.visitInsn(ARRAYLENGTH);
            mv.visitJumpInsn(IF_ICMPGE, end);
            load(f.hidden1);
            load(f.hidden2);
            mv.visitInsn(f.local.type.asm().getOpcode(IALOAD));
            store(f.local);
            declared(f.local);
            loops.push(new Label[]{end, cont});
            block(f.body);
            loops.pop();
            mv.visitLabel(cont);
            mv.visitIincInsn(f.hidden2.slot, 1);
        }
        mv.visitJumpInsn(GOTO, start);
        mv.visitLabel(end);
    }

    private void assign(Assign a) {
        Type tt = a.target.type;
        boolean compound = !a.op.equals("=");
        switch (a.target) {
            case Name n when n.local != null -> {
                if (compound) { load(n.local); compound(tt, a.op, a.value); }
                else exprAs(a.value, tt);
                store(n.local);
            }
            case Name n -> {
                mv.visitVarInsn(ALOAD, 0);
                putField(n.field, a, compound);
            }
            case Field f -> {
                expr(f.target);
                putField(f.field, a, compound);
            }
            case Index ix -> {
                expr(ix.target);
                exprAs(ix.index, Type.INT);
                if (compound) {
                    mv.visitInsn(DUP2);
                    mv.visitInsn(tt.asm().getOpcode(IALOAD));
                    compound(tt, a.op, a.value);
                } else {
                    exprAs(a.value, tt);
                }
                mv.visitInsn(tt.asm().getOpcode(IASTORE));
            }
            default -> throw new IllegalStateException();
        }
    }

    /** На стеке уже лежит объект-владелец поля. */
    private void putField(Checker.FieldInfo f, Assign a, boolean compound) {
        if (compound) {
            mv.visitInsn(DUP);
            mv.visitFieldInsn(GETFIELD, f.owner.name, f.name, f.type.desc());
            compound(f.type, a.op, a.value);
        } else {
            exprAs(a.value, f.type);
        }
        mv.visitFieldInsn(PUTFIELD, f.owner.name, f.name, f.type.desc());
    }

    /** На стеке текущее значение типа tt; вычисляет tt op= value. */
    private void compound(Type tt, String op, Expr value) {
        String bop = op.substring(0, 1);
        if (tt.is(Type.K.STRING)) {
            mv.visitTypeInsn(NEW, BUILDER);
            mv.visitInsn(DUP_X1);
            mv.visitInsn(SWAP);
            mv.visitMethodInsn(INVOKESPECIAL, BUILDER, "<init>", "(Ljava/lang/String;)V", false);
            appendConcat(value);
            mv.visitMethodInsn(INVOKEVIRTUAL, BUILDER, "toString", "()Ljava/lang/String;", false);
            return;
        }
        exprAs(value, tt);
        mv.visitInsn(tt.asm().getOpcode(arith(bop)));
    }

    // ================= выражения =================

    private void pop(Type t) {
        if (t.is(Type.K.VOID)) return;
        mv.visitInsn(t.is(Type.K.DOUBLE) ? POP2 : POP);
    }

    private void pushInt(int v) {
        if (v >= -1 && v <= 5) mv.visitInsn(ICONST_0 + v);
        else if (v >= Byte.MIN_VALUE && v <= Byte.MAX_VALUE) mv.visitIntInsn(BIPUSH, v);
        else if (v >= Short.MIN_VALUE && v <= Short.MAX_VALUE) mv.visitIntInsn(SIPUSH, v);
        else mv.visitLdcInsn(v);
    }

    private void pushDefault(Type t) {
        switch (t.k) {
            case INT, BOOL -> mv.visitInsn(ICONST_0);
            case DOUBLE -> mv.visitInsn(DCONST_0);
            default -> mv.visitInsn(ACONST_NULL);
        }
    }

    private void exprAs(Expr e, Type target) {
        expr(e);
        if (e.type.is(Type.K.INT) && target.is(Type.K.DOUBLE)) mv.visitInsn(I2D);
    }

    private void expr(Expr e) {
        switch (e) {
            case IntLit l -> pushInt(l.v);
            case DoubleLit l -> {
                if (l.v == 0.0 && 1 / l.v > 0) mv.visitInsn(DCONST_0);
                else if (l.v == 1.0) mv.visitInsn(DCONST_1);
                else mv.visitLdcInsn(l.v);
            }
            case StrLit l -> mv.visitLdcInsn(l.v);
            case BoolLit l -> mv.visitInsn(l.v ? ICONST_1 : ICONST_0);
            case NullLit l -> mv.visitInsn(ACONST_NULL);
            case This t -> mv.visitVarInsn(ALOAD, 0);
            case Name n -> {
                if (n.local != null) load(n.local);
                else {
                    mv.visitVarInsn(ALOAD, 0);
                    mv.visitFieldInsn(GETFIELD, n.field.owner.name, n.field.name, n.field.type.desc());
                }
            }
            case Binary b -> binary(b);
            case Unary u -> {
                if (u.op.equals("not")) materializeBool(u);
                else {
                    expr(u.e);
                    mv.visitInsn(u.type.is(Type.K.DOUBLE) ? DNEG : INEG);
                }
            }
            case Call c -> call(c);
            case MethodCall mc -> {
                expr(mc.target);
                if (mc.method != null) {
                    for (int i = 0; i < mc.args.size(); i++) exprAs(mc.args.get(i), mc.method.params.get(i));
                    mv.visitMethodInsn(INVOKEVIRTUAL, mc.method.owner.name, mc.name, mc.method.desc(), false);
                } else {
                    for (int i = 0; i < mc.args.size(); i++) {
                        Type pt = mc.javaParams.get(i);
                        if (pt != null) exprAs(mc.args.get(i), pt);
                        else expr(mc.args.get(i));
                    }
                    mv.visitMethodInsn(INVOKEVIRTUAL, STRING, mc.name,
                            org.objectweb.asm.Type.getMethodDescriptor(mc.javaMethod), false);
                    if (mc.charToString) mv.visitMethodInsn(INVOKESTATIC, STRING, "valueOf", "(C)Ljava/lang/String;", false);
                }
            }
            case SuperMethodCall sc -> {
                mv.visitVarInsn(ALOAD, 0);
                for (int i = 0; i < sc.args.size(); i++) exprAs(sc.args.get(i), sc.method.params.get(i));
                mv.visitMethodInsn(INVOKESPECIAL, sc.superInternal, sc.name, sc.method.desc(), false);
            }
            case Field f -> {
                expr(f.target);
                if (f.arrayLength) mv.visitInsn(ARRAYLENGTH);
                else mv.visitFieldInsn(GETFIELD, f.field.owner.name, f.field.name, f.field.type.desc());
            }
            case Index ix -> {
                expr(ix.target);
                exprAs(ix.index, Type.INT);
                mv.visitInsn(ix.type.asm().getOpcode(IALOAD));
            }
            case New n -> {
                mv.visitTypeInsn(NEW, n.cls);
                mv.visitInsn(DUP);
                List<Type> ps = n.info.init == null ? List.of() : n.info.init.params;
                for (int i = 0; i < n.args.size(); i++) exprAs(n.args.get(i), ps.get(i));
                mv.visitMethodInsn(INVOKESPECIAL, n.cls, "<init>", n.info.init == null ? "()V" : n.info.init.desc(), false);
            }
            case NewArray na -> {
                exprAs(na.size, Type.INT);
                newArray(na.elemType);
            }
            case ArrayLit al -> {
                pushInt(al.elems.size());
                newArray(al.elemType);
                for (int i = 0; i < al.elems.size(); i++) {
                    mv.visitInsn(DUP);
                    pushInt(i);
                    exprAs(al.elems.get(i), al.elemType);
                    mv.visitInsn(al.elemType.asm().getOpcode(IASTORE));
                }
            }
            default -> throw new IllegalStateException("неизвестное выражение " + e);
        }
    }

    private void newArray(Type elem) {
        switch (elem.k) {
            case INT -> mv.visitIntInsn(NEWARRAY, T_INT);
            case DOUBLE -> mv.visitIntInsn(NEWARRAY, T_DOUBLE);
            case BOOL -> mv.visitIntInsn(NEWARRAY, T_BOOLEAN);
            default -> mv.visitTypeInsn(ANEWARRAY, elem.internal());
        }
    }

    private static int arith(String op) {
        return switch (op) {
            case "+" -> IADD;
            case "-" -> ISUB;
            case "*" -> IMUL;
            case "/" -> IDIV;
            case "%" -> IREM;
            default -> throw new IllegalStateException(op);
        };
    }

    private void binary(Binary b) {
        if (b.type.is(Type.K.BOOL)) { materializeBool(b); return; }
        if (b.type.is(Type.K.STRING)) { concat(b); return; }
        exprAs(b.l, b.type);
        exprAs(b.r, b.type);
        mv.visitInsn(b.type.asm().getOpcode(arith(b.op)));
    }

    // ---------- строки ----------

    private void collectConcat(Expr e, List<Expr> out) {
        if (e instanceof Binary b && b.op.equals("+") && b.type.is(Type.K.STRING)) {
            collectConcat(b.l, out);
            collectConcat(b.r, out);
        } else {
            out.add(e);
        }
    }

    /** Java 8 StringBuilder works on both BJVM and a standard JVM. */
    private void concat(Binary b) {
        List<Expr> parts = new ArrayList<>();
        collectConcat(b, parts);
        mv.visitTypeInsn(NEW, BUILDER);
        mv.visitInsn(DUP);
        mv.visitMethodInsn(INVOKESPECIAL, BUILDER, "<init>", "()V", false);
        for (Expr p : parts) appendConcat(p);
        mv.visitMethodInsn(INVOKEVIRTUAL, BUILDER, "toString", "()Ljava/lang/String;", false);
    }

    private void appendConcat(Expr e) {
        String d = concatLeaf(e);
        if (d.startsWith("L") && !d.equals("Ljava/lang/String;")) d = "Ljava/lang/Object;";
        mv.visitMethodInsn(INVOKEVIRTUAL, BUILDER, "append", "(" + d + ")L" + BUILDER + ";", false);
    }

    /** Кладёт значение на стек в виде, пригодном для склейки/печати, и возвращает его дескриптор. */
    private String concatLeaf(Expr e) {
        expr(e);
        return printable(e.type);
    }

    private String printable(Type t) {
        switch (t.k) {
            case INT, DOUBLE, BOOL, STRING -> { return t.desc(); }
            case ARRAY -> {
                boolean deep = t.elem.is(Type.K.ARRAY);
                String arg = t.elem.isRef() ? "[Ljava/lang/Object;" : t.desc();
                mv.visitMethodInsn(INVOKESTATIC, "java/util/Arrays", deep ? "deepToString" : "toString",
                        "(" + arg + ")Ljava/lang/String;", false);
                return "Ljava/lang/String;";
            }
            default -> { return "Ljava/lang/Object;"; }
        }
    }

    // ---------- вызовы ----------

    private void call(Call c) {
        if (c.builtin != null) { builtin(c); return; }
        Checker.MethodInfo m = c.target;
        if (c.implicitThis) mv.visitVarInsn(ALOAD, 0);
        for (int i = 0; i < c.args.size(); i++) exprAs(c.args.get(i), m.params.get(i));
        if (c.implicitThis) mv.visitMethodInsn(INVOKEVIRTUAL, m.owner.name, m.name, m.desc(), false);
        else mv.visitMethodInsn(INVOKESTATIC, mainClass, m.name, m.desc(), false);
    }

    private void builtin(Call c) {
        switch (c.builtin) {
            case "print" -> {
                mv.visitFieldInsn(GETSTATIC, "java/lang/System", "out", "Ljava/io/PrintStream;");
                if (c.args.isEmpty()) {
                    mv.visitMethodInsn(INVOKEVIRTUAL, "java/io/PrintStream", "println", "()V", false);
                } else {
                    String d = concatLeaf(c.args.get(0));
                    mv.visitMethodInsn(INVOKEVIRTUAL, "java/io/PrintStream", "println", "(" + d + ")V", false);
                }
            }
            case "input" -> mv.visitMethodInsn(INVOKESTATIC, mainClass, "$input", "()Ljava/lang/String;", false);
            case "str" -> {
                String d = concatLeaf(c.args.get(0));
                if (d.startsWith("L")) d = "Ljava/lang/Object;";
                mv.visitMethodInsn(INVOKESTATIC, STRING, "valueOf", "(" + d + ")Ljava/lang/String;", false);
            }
            case "toInt" -> {
                Expr a = c.args.get(0);
                expr(a);
                if (a.type.is(Type.K.DOUBLE)) mv.visitInsn(D2I);
                else if (a.type.is(Type.K.STRING)) {
                    mv.visitMethodInsn(INVOKEVIRTUAL, STRING, "trim", "()Ljava/lang/String;", false);
                    mv.visitMethodInsn(INVOKESTATIC, "java/lang/Integer", "parseInt", "(Ljava/lang/String;)I", false);
                }
            }
            case "toDouble" -> {
                Expr a = c.args.get(0);
                expr(a);
                if (a.type.is(Type.K.INT)) mv.visitInsn(I2D);
                else if (a.type.is(Type.K.STRING)) {
                    mv.visitMethodInsn(INVOKEVIRTUAL, STRING, "trim", "()Ljava/lang/String;", false);
                    mv.visitMethodInsn(INVOKESTATIC, "java/lang/Double", "parseDouble", "(Ljava/lang/String;)D", false);
                }
            }
            default -> throw new IllegalStateException(c.builtin);
        }
    }

    // ---------- условия и переходы ----------

    private void materializeBool(Expr e) {
        Label f = new Label(), end = new Label();
        jump(e, false, f);
        mv.visitInsn(ICONST_1);
        mv.visitJumpInsn(GOTO, end);
        mv.visitLabel(f);
        mv.visitInsn(ICONST_0);
        mv.visitLabel(end);
    }

    /** Перейти на target, если значение e == when. Иначе — провалиться дальше. */
    private void jump(Expr e, boolean when, Label target) {
        if (e instanceof BoolLit b) {
            if (b.v == when) mv.visitJumpInsn(GOTO, target);
            return;
        }
        if (e instanceof Unary u && u.op.equals("not")) { jump(u.e, !when, target); return; }
        if (e instanceof Binary b) {
            switch (b.op) {
                case "and" -> {
                    if (!when) { jump(b.l, false, target); jump(b.r, false, target); }
                    else { Label skip = new Label(); jump(b.l, false, skip); jump(b.r, true, target); mv.visitLabel(skip); }
                    return;
                }
                case "or" -> {
                    if (when) { jump(b.l, true, target); jump(b.r, true, target); }
                    else { Label skip = new Label(); jump(b.l, true, skip); jump(b.r, false, target); mv.visitLabel(skip); }
                    return;
                }
                case "==", "!=", "<", "<=", ">", ">=" -> { compare(b, when ? b.op : negate(b.op), target); return; }
                default -> {}
            }
        }
        expr(e);
        mv.visitJumpInsn(when ? IFNE : IFEQ, target);
    }

    private static String negate(String op) {
        return switch (op) {
            case "==" -> "!=";
            case "!=" -> "==";
            case "<" -> ">=";
            case ">=" -> "<";
            case ">" -> "<=";
            case "<=" -> ">";
            default -> throw new IllegalStateException(op);
        };
    }

    private static int icmp(String op) {
        return switch (op) {
            case "==" -> IF_ICMPEQ;
            case "!=" -> IF_ICMPNE;
            case "<" -> IF_ICMPLT;
            case "<=" -> IF_ICMPLE;
            case ">" -> IF_ICMPGT;
            default -> IF_ICMPGE;
        };
    }

    private static int ifz(String op) {
        return switch (op) {
            case "==" -> IFEQ;
            case "!=" -> IFNE;
            case "<" -> IFLT;
            case "<=" -> IFLE;
            case ">" -> IFGT;
            default -> IFGE;
        };
    }

    /** Переход, если (l op r) истинно. */
    private void compare(Binary b, String op, Label target) {
        Type ot = b.operandType;
        switch (ot.k) {
            case INT, BOOL -> {
                exprAs(b.l, ot);
                exprAs(b.r, ot);
                mv.visitJumpInsn(icmp(op), target);
            }
            case DOUBLE -> {
                exprAs(b.l, ot);
                exprAs(b.r, ot);
                mv.visitInsn(op.equals("<") || op.equals("<=") ? DCMPG : DCMPL);
                mv.visitJumpInsn(ifz(op), target);
            }
            case STRING -> {
                expr(b.l);
                expr(b.r);
                if (op.equals("==") || op.equals("!=")) {
                    // строки сравниваются по значению, а не по ссылке
                    mv.visitMethodInsn(INVOKESTATIC, "java/util/Objects", "equals",
                            "(Ljava/lang/Object;Ljava/lang/Object;)Z", false);
                    mv.visitJumpInsn(op.equals("==") ? IFNE : IFEQ, target);
                } else {
                    mv.visitMethodInsn(INVOKEVIRTUAL, STRING, "compareTo", "(Ljava/lang/String;)I", false);
                    mv.visitJumpInsn(ifz(op), target);
                }
            }
            default -> {
                expr(b.l);
                expr(b.r);
                mv.visitJumpInsn(op.equals("==") ? IF_ACMPEQ : IF_ACMPNE, target);
            }
        }
    }
}
