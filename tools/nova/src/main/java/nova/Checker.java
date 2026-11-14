package nova;

import static nova.Ast.*;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Разрешение имён, проверка и вывод типов. */
final class Checker {

    static final class ClassInfo {
        final String name;
        final ClassDecl decl;
        ClassInfo sup;
        final Map<String, FieldInfo> fields = new LinkedHashMap<>();
        final Map<String, MethodInfo> methods = new LinkedHashMap<>();
        MethodInfo init; // null => конструктор без параметров

        ClassInfo(String name, ClassDecl decl) { this.name = name; this.decl = decl; }

        FieldInfo findField(String n) {
            for (ClassInfo c = this; c != null; c = c.sup) {
                FieldInfo f = c.fields.get(n);
                if (f != null) return f;
            }
            return null;
        }

        MethodInfo findMethod(String n) {
            for (ClassInfo c = this; c != null; c = c.sup) {
                MethodInfo m = c.methods.get(n);
                if (m != null) return m;
            }
            return null;
        }

        boolean isSubOf(ClassInfo o) {
            for (ClassInfo c = this; c != null; c = c.sup) if (c == o) return true;
            return false;
        }

        String superInternal() { return sup == null ? "java/lang/Object" : sup.name; }

        Type type() { return Type.cls(name); }
    }

    static final class FieldInfo {
        String name;
        Type type;
        ClassInfo owner;
    }

    static final class MethodInfo {
        String name;
        final List<Type> params = new ArrayList<>();
        Type ret;
        ClassInfo owner; // null => глобальная функция
        FuncDecl decl;

        String desc() {
            StringBuilder sb = new StringBuilder("(");
            for (Type t : params) sb.append(t.desc());
            return sb.append(')').append(ret.desc()).toString();
        }
    }

    static final class Local {
        final String name;
        final Type type;
        final int slot;
        Local(String name, Type type, int slot) { this.name = name; this.type = type; this.slot = slot; }
    }

    private static final Set<String> BUILTINS = Set.of("print", "input", "str", "toInt", "toDouble");

    final Map<String, ClassInfo> classes = new LinkedHashMap<>();
    final Map<String, MethodInfo> funcs = new LinkedHashMap<>();
    MethodInfo mainFunc;

    // контекст текущей функции
    private ClassInfo curClass;
    private MethodInfo curMethod;
    private boolean isStatic;
    private int nextSlot;
    private int loopDepth;
    private Deque<Map<String, Local>> scopes = new ArrayDeque<>();

    private static CompileError err(Node n, String msg) { return new CompileError(n.line, n.col, msg); }

    // ================= объявления =================

    void check(Program prog) {
        for (ClassDecl cd : prog.classes) {
            if (classes.containsKey(cd.name)) throw err(cd, "класс '" + cd.name + "' уже объявлен");
            classes.put(cd.name, new ClassInfo(cd.name, cd));
        }
        for (ClassInfo ci : classes.values()) {
            if (ci.decl.superName != null) {
                ci.sup = classes.get(ci.decl.superName);
                if (ci.sup == null) throw err(ci.decl, "неизвестный базовый класс '" + ci.decl.superName + "'");
            }
        }
        for (ClassInfo ci : classes.values()) {
            ClassInfo c = ci.sup;
            while (c != null) {
                if (c == ci) throw err(ci.decl, "циклическое наследование у класса '" + ci.name + "'");
                c = c.sup;
            }
        }
        for (ClassInfo ci : classes.values()) {
            for (FieldDecl fd : ci.decl.fields) {
                if (ci.fields.containsKey(fd.name)) throw err(fd, "поле '" + fd.name + "' уже объявлено");
                FieldInfo fi = new FieldInfo();
                fi.name = fd.name;
                fi.type = resolve(fd.type, false);
                fi.owner = ci;
                ci.fields.put(fd.name, fi);
            }
            for (FuncDecl f : ci.decl.methods) {
                if (ci.methods.containsKey(f.name)) throw err(f, "метод '" + f.name + "' уже объявлен (перегрузка пока не поддерживается)");
                ci.methods.put(f.name, signature(f, ci));
            }
            if (ci.decl.init != null) ci.init = signature(ci.decl.init, ci);
        }
        // наследование: поля не дублируются, переопределения совпадают по сигнатуре
        for (ClassInfo ci : classes.values()) {
            if (ci.sup == null) continue;
            for (FieldInfo f : ci.fields.values()) {
                if (ci.sup.findField(f.name) != null)
                    throw err(fieldDecl(ci, f.name), "поле '" + f.name + "' уже есть в базовом классе");
            }
            for (MethodInfo m : ci.methods.values()) {
                MethodInfo base = ci.sup.findMethod(m.name);
                if (base != null && (!base.params.equals(m.params) || !base.ret.equals(m.ret)))
                    throw err(m.decl, "метод '" + m.name + "' переопределяет " + base.owner.name + "." + m.name
                            + " с другой сигнатурой");
            }
        }
        for (FuncDecl f : prog.funcs) {
            if (funcs.containsKey(f.name)) throw err(f, "функция '" + f.name + "' уже объявлена (перегрузка пока не поддерживается)");
            if (BUILTINS.contains(f.name)) throw err(f, "'" + f.name + "' — встроенная функция, выберите другое имя");
            funcs.put(f.name, signature(f, null));
        }
        mainFunc = funcs.get("main");
        if (mainFunc == null) throw new CompileError(1, 1, "нет точки входа: объявите 'void main():'");
        boolean okMain = mainFunc.ret.is(Type.K.VOID) && (mainFunc.params.isEmpty()
                || (mainFunc.params.size() == 1 && mainFunc.params.get(0).equals(Type.arr(Type.STRING))));
        if (!okMain) throw err(mainFunc.decl, "main должна быть 'void main()' или 'void main(string[] args)'");

        // ================= тела =================
        for (ClassInfo ci : classes.values()) {
            for (FieldDecl fd : ci.decl.fields) {
                if (fd.init == null) continue;
                enter(ci, null);
                FieldInfo fi = ci.fields.get(fd.name);
                expectAssignable(expr(fd.init), fi.type, fd.init);
            }
            if (ci.decl.init != null) checkInit(ci);
            else if (ci.sup != null && needsArgs(ci.sup))
                throw err(ci.decl, "базовый класс " + ci.sup.name + " требует аргументы конструктора: объявите init и вызовите super(...)");
            for (FuncDecl f : ci.decl.methods) checkBody(f, ci.methods.get(f.name), ci);
        }
        for (FuncDecl f : prog.funcs) checkBody(f, funcs.get(f.name), null);
    }

    private static boolean needsArgs(ClassInfo c) { return c.init != null && !c.init.params.isEmpty(); }

    private static FieldDecl fieldDecl(ClassInfo ci, String name) {
        for (FieldDecl fd : ci.decl.fields) if (fd.name.equals(name)) return fd;
        return null;
    }

    private MethodInfo signature(FuncDecl f, ClassInfo owner) {
        MethodInfo m = new MethodInfo();
        m.name = f.name;
        m.owner = owner;
        m.decl = f;
        m.ret = resolve(f.ret, true);
        for (Param p : f.params) m.params.add(resolve(p.type, false));
        f.info = m;
        return m;
    }

    Type resolve(TypeRef r, boolean allowVoid) {
        Type base = switch (r.name) {
            case "int" -> Type.INT;
            case "double" -> Type.DOUBLE;
            case "bool" -> Type.BOOL;
            case "string" -> Type.STRING;
            case "void" -> Type.VOID;
            default -> {
                if (!classes.containsKey(r.name)) throw err(r, "неизвестный тип '" + r.name + "'");
                yield Type.cls(r.name);
            }
        };
        if (base.is(Type.K.VOID) && (!allowVoid || r.dims > 0)) throw err(r, "тип void здесь недопустим");
        for (int i = 0; i < r.dims; i++) base = Type.arr(base);
        return base;
    }

    // ================= функции =================

    private void enter(ClassInfo cls, MethodInfo m) {
        curClass = cls;
        curMethod = m;
        isStatic = cls == null;
        nextSlot = isStatic ? 0 : 1;
        loopDepth = 0;
        scopes = new ArrayDeque<>();
        scopes.push(new HashMap<>());
    }

    private void declareParams(FuncDecl f, MethodInfo m) {
        for (int i = 0; i < f.params.size(); i++) {
            Param p = f.params.get(i);
            p.local = declare(p, p.name, m.params.get(i));
        }
    }

    private void checkInit(ClassInfo ci) {
        FuncDecl f = ci.decl.init;
        MethodInfo m = ci.init;
        enter(ci, m);
        declareParams(f, m);
        for (int i = 0; i < f.body.size(); i++) {
            if (f.body.get(i) instanceof SuperCall sc && i != 0)
                throw err(sc, "super(...) должен быть первым оператором в init");
        }
        boolean hasSuper = !f.body.isEmpty() && f.body.get(0) instanceof SuperCall;
        if (!hasSuper && ci.sup != null && needsArgs(ci.sup))
            throw err(f, "базовый класс " + ci.sup.name + " требует аргументы: добавьте super(...) первой строкой init");
        block(f.body, false);
    }

    private void checkBody(FuncDecl f, MethodInfo m, ClassInfo owner) {
        enter(owner, m);
        declareParams(f, m);
        block(f.body, false);
        if (!m.ret.is(Type.K.VOID) && !alwaysReturns(f.body))
            throw err(f, "функция '" + f.name + "' не во всех ветках возвращает значение типа " + m.ret);
    }

    private Local declare(Node at, String name, Type type) {
        for (Map<String, Local> s : scopes) {
            if (s.containsKey(name)) throw err(at, "переменная '" + name + "' уже объявлена в этой функции");
        }
        Local l = new Local(name, type, nextSlot);
        nextSlot += type.is(Type.K.DOUBLE) ? 2 : 1;
        scopes.peek().put(name, l);
        return l;
    }

    private Local hidden(Type type) {
        Local l = new Local("$hidden" + nextSlot, type, nextSlot);
        nextSlot += type.is(Type.K.DOUBLE) ? 2 : 1;
        return l;
    }

    private Local lookupLocal(String name) {
        for (Map<String, Local> s : scopes) {
            Local l = s.get(name);
            if (l != null) return l;
        }
        return null;
    }

    private void block(List<Stmt> stmts, boolean newScope) {
        if (newScope) scopes.push(new HashMap<>());
        for (Stmt s : stmts) stmt(s);
        if (newScope) scopes.pop();
    }

    // ================= операторы =================

    private void stmt(Stmt s) {
        switch (s) {
            case VarDecl d -> {
                Type t;
                if (d.declType == null) {
                    t = expr(d.init);
                    if (t.is(Type.K.NULL)) throw err(d, "нельзя вывести тип из null: укажите тип явно");
                    if (t.is(Type.K.VOID)) throw err(d.init, "выражение ничего не возвращает (void)");
                } else {
                    t = resolve(d.declType, false);
                    if (d.init != null) expectAssignable(expr(d.init), t, d.init);
                }
                d.local = declare(d, d.name, t);
            }
            case Assign a -> {
                Type tt = expr(a.target);
                if (a.target instanceof Field f && f.arrayLength) throw err(a, "length массива нельзя изменить");
                Type vt = expr(a.value);
                if (a.op.equals("=")) {
                    expectAssignable(vt, tt, a.value);
                } else {
                    String op = a.op.substring(0, 1);
                    Type rt = binaryType(op, tt, vt, a, null);
                    if (!assignable(rt, tt))
                        throw err(a, "результат '" + tt + " " + op + " " + vt + "' имеет тип " + rt + " и не помещается в " + tt);
                }
            }
            case If i -> {
                expectBool(i.cond);
                block(i.then, true);
                if (i.els != null) block(i.els, true);
            }
            case While w -> {
                expectBool(w.cond);
                loopDepth++;
                block(w.body, true);
                loopDepth--;
            }
            case For f -> {
                if (f.to != null) {
                    expectAssignable(expr(f.from), Type.INT, f.from);
                    expectAssignable(expr(f.to), Type.INT, f.to);
                    f.hidden1 = hidden(Type.INT);
                    scopes.push(new HashMap<>());
                    f.local = declare(f, f.var, Type.INT);
                } else {
                    Type at = expr(f.from);
                    if (!at.is(Type.K.ARRAY)) throw err(f.from, "for ... in работает с массивами и диапазонами a..b, а получен " + at);
                    f.hidden1 = hidden(at);
                    f.hidden2 = hidden(Type.INT);
                    scopes.push(new HashMap<>());
                    f.local = declare(f, f.var, at.elem);
                }
                loopDepth++;
                block(f.body, true);
                loopDepth--;
                scopes.pop();
            }
            case Return r -> {
                Type ret = curMethod == null ? Type.VOID : curMethod.ret;
                if (r.value == null) {
                    if (!ret.is(Type.K.VOID)) throw err(r, "нужно вернуть значение типа " + ret);
                } else {
                    if (ret.is(Type.K.VOID)) throw err(r.value, "функция void не может возвращать значение");
                    expectAssignable(expr(r.value), ret, r.value);
                }
            }
            case Break b -> { if (loopDepth == 0) throw err(b, "break вне цикла"); }
            case Continue c -> { if (loopDepth == 0) throw err(c, "continue вне цикла"); }
            case Pass p -> {}
            case ExprStmt es -> {
                Expr e = es.expr;
                if (!(e instanceof Call || e instanceof MethodCall || e instanceof SuperMethodCall || e instanceof New))
                    throw err(e, "это выражение ничего не делает: его результат не используется");
                expr(e);
            }
            case SuperCall sc -> {
                if (curMethod == null || !curMethod.decl.isInit) throw err(sc, "super(...) можно вызывать только в init");
                if (curClass.sup == null) throw err(sc, "у класса " + curClass.name + " нет базового класса");
                sc.ctor = curClass.sup.init;
                List<Type> params = sc.ctor == null ? List.of() : sc.ctor.params;
                args(sc.args, params, sc, "конструктор " + curClass.sup.name);
            }
            default -> throw err(s, "неизвестный оператор");
        }
    }

    private boolean alwaysReturns(List<Stmt> stmts) {
        for (Stmt s : stmts) if (terminates(s)) return true;
        return false;
    }

    private boolean terminates(Stmt s) {
        if (s instanceof Return) return true;
        if (s instanceof If i) return i.els != null && alwaysReturns(i.then) && alwaysReturns(i.els);
        if (s instanceof While w) return w.cond instanceof BoolLit b && b.v && !hasBreak(w.body);
        return false;
    }

    private boolean hasBreak(List<Stmt> stmts) {
        if (stmts == null) return false;
        for (Stmt s : stmts) {
            if (s instanceof Break) return true;
            if (s instanceof If i && (hasBreak(i.then) || hasBreak(i.els))) return true;
        }
        return false;
    }

    // ================= выражения =================

    static boolean assignable(Type from, Type to, Map<String, ClassInfo> classes) {
        if (from.equals(to)) return true;
        if (from.is(Type.K.INT) && to.is(Type.K.DOUBLE)) return true;
        if (from.is(Type.K.NULL) && to.isRef()) return true;
        if (from.is(Type.K.CLASS) && to.is(Type.K.CLASS)) return classes.get(from.cls).isSubOf(classes.get(to.cls));
        return false;
    }

    boolean assignable(Type from, Type to) { return assignable(from, to, classes); }

    /** Ближайший общий базовый класс двух классов или null. */
    private Type commonBase(Type a, Type b) {
        if (!a.is(Type.K.CLASS) || !b.is(Type.K.CLASS)) return null;
        ClassInfo cb = classes.get(b.cls);
        for (ClassInfo c = classes.get(a.cls); c != null; c = c.sup) if (cb.isSubOf(c)) return c.type();
        return null;
    }

    private void expectAssignable(Type from, Type to, Node at) {
        if (!assignable(from, to)) {
            String hint = from.is(Type.K.DOUBLE) && to.is(Type.K.INT) ? " (используйте toInt(...))"
                    : to.is(Type.K.STRING) && !from.isRef() ? " (используйте str(...))" : "";
            throw err(at, "ожидался тип " + to + ", а получен " + from + hint);
        }
    }

    private void expectBool(Expr e) {
        Type t = expr(e);
        if (!t.is(Type.K.BOOL)) throw err(e, "условие должно быть bool, а получен " + t);
    }

    private void args(List<Expr> args, List<Type> params, Node at, String what) {
        if (args.size() != params.size())
            throw err(at, what + " ожидает " + params.size() + " арг., а передано " + args.size());
        for (int i = 0; i < args.size(); i++) expectAssignable(expr(args.get(i)), params.get(i), args.get(i));
    }

    private Type expr(Expr e) {
        Type t = exprInner(e);
        e.type = t;
        return t;
    }

    private Type exprInner(Expr e) {
        switch (e) {
            case IntLit l -> { return Type.INT; }
            case DoubleLit l -> { return Type.DOUBLE; }
            case StrLit l -> { return Type.STRING; }
            case BoolLit l -> { return Type.BOOL; }
            case NullLit l -> { return Type.NULL; }
            case This t -> {
                if (isStatic) throw err(t, "this доступен только внутри методов класса");
                return curClass.type();
            }
            case Name n -> {
                Local l = lookupLocal(n.name);
                if (l != null) { n.local = l; return l.type; }
                if (!isStatic) {
                    FieldInfo f = curClass.findField(n.name);
                    if (f != null) { n.field = f; return f.type; }
                }
                throw err(n, "неизвестная переменная '" + n.name + "'");
            }
            case Binary b -> {
                Type lt = expr(b.l), rt = expr(b.r);
                return binaryType(b.op, lt, rt, b, b);
            }
            case Unary u -> {
                Type t = expr(u.e);
                if (u.op.equals("not")) {
                    if (!t.is(Type.K.BOOL)) throw err(u, "'not' применим только к bool, а получен " + t);
                    return Type.BOOL;
                }
                if (!t.isNum()) throw err(u, "унарный минус применим только к числам, а получен " + t);
                return t;
            }
            case Call c -> { return call(c); }
            case MethodCall mc -> { return methodCall(mc); }
            case SuperMethodCall sc -> {
                if (isStatic || curClass.sup == null) throw err(sc, "super.метод() доступен только в классе с базовым классом");
                MethodInfo m = curClass.sup.findMethod(sc.name);
                if (m == null) throw err(sc, "в базовых классах нет метода '" + sc.name + "'");
                args(sc.args, m.params, sc, "метод " + sc.name);
                sc.method = m;
                sc.superInternal = curClass.sup.name;
                return m.ret;
            }
            case Field f -> {
                Type tt = expr(f.target);
                if (tt.is(Type.K.ARRAY) && f.name.equals("length")) { f.arrayLength = true; return Type.INT; }
                if (tt.is(Type.K.STRING) && f.name.equals("length"))
                    throw err(f, "у строки это метод: используйте s.length()");
                if (!tt.is(Type.K.CLASS)) throw err(f, "у типа " + tt + " нет поля '" + f.name + "'");
                FieldInfo fi = classes.get(tt.cls).findField(f.name);
                if (fi == null) throw err(f, "у класса " + tt.cls + " нет поля '" + f.name + "'");
                f.field = fi;
                return fi.type;
            }
            case Index ix -> {
                Type tt = expr(ix.target);
                if (!tt.is(Type.K.ARRAY)) throw err(ix, "индексировать можно только массив, а получен " + tt);
                expectAssignable(expr(ix.index), Type.INT, ix.index);
                return tt.elem;
            }
            case New n -> {
                ClassInfo ci = classes.get(n.cls);
                if (ci == null) throw err(n, "неизвестный класс '" + n.cls + "'");
                n.info = ci;
                args(n.args, ci.init == null ? List.of() : ci.init.params, n, "конструктор " + n.cls);
                return ci.type();
            }
            case NewArray na -> {
                na.elemType = resolve(na.elem, false);
                expectAssignable(expr(na.size), Type.INT, na.size);
                return Type.arr(na.elemType);
            }
            case ArrayLit al -> {
                if (al.elems.isEmpty()) throw err(al, "нельзя вывести тип пустого массива: используйте new T[0]");
                Type t = null;
                for (Expr x : al.elems) {
                    Type xt = expr(x);
                    if (xt.is(Type.K.VOID)) throw err(x, "void не может быть элементом массива");
                    if (xt.is(Type.K.NULL)) continue;
                    if (t == null) t = xt;
                    else if (assignable(xt, t)) { /* ok */ }
                    else if (assignable(t, xt)) t = xt;
                    else if (commonBase(t, xt) != null) t = commonBase(t, xt);
                    else throw err(x, "элементы массива разных типов: " + t + " и " + xt);
                }
                if (t == null) throw err(al, "нельзя вывести тип массива из одних null");
                for (Expr x : al.elems) expectAssignable(x.type, t, x);
                al.elemType = t;
                return Type.arr(t);
            }
            default -> throw err(e, "неизвестное выражение");
        }
    }

    /** Тип бинарной операции. b != null => записать operandType. */
    private Type binaryType(String op, Type lt, Type rt, Node at, Binary b) {
        if (lt.is(Type.K.VOID) || rt.is(Type.K.VOID)) throw err(at, "void нельзя использовать в выражении");
        String bad = "оператор '" + op + "' не применим к " + lt + " и " + rt;
        switch (op) {
            case "+" -> {
                if (lt.is(Type.K.STRING) || rt.is(Type.K.STRING)) return Type.STRING;
                if (lt.isNum() && rt.isNum()) return num(lt, rt, b);
                throw err(at, bad);
            }
            case "-", "*", "/", "%" -> {
                if (lt.isNum() && rt.isNum()) return num(lt, rt, b);
                throw err(at, bad);
            }
            case "<", "<=", ">", ">=" -> {
                if (lt.isNum() && rt.isNum()) { num(lt, rt, b); return Type.BOOL; }
                if (lt.is(Type.K.STRING) && rt.is(Type.K.STRING)) { if (b != null) b.operandType = Type.STRING; return Type.BOOL; }
                throw err(at, bad);
            }
            case "==", "!=" -> {
                Type ot;
                if (lt.isNum() && rt.isNum()) ot = (lt.is(Type.K.DOUBLE) || rt.is(Type.K.DOUBLE)) ? Type.DOUBLE : Type.INT;
                else if (lt.is(Type.K.BOOL) && rt.is(Type.K.BOOL)) ot = Type.BOOL;
                else if ((lt.is(Type.K.NULL) && rt.isRef()) || (rt.is(Type.K.NULL) && lt.isRef())) ot = Type.NULL;
                else if (lt.is(Type.K.STRING) && rt.is(Type.K.STRING)) ot = Type.STRING;
                else if (lt.isRef() && rt.isRef() && (assignable(lt, rt) || assignable(rt, lt))) ot = lt;
                else throw err(at, "нельзя сравнивать " + lt + " и " + rt);
                if (b != null) b.operandType = ot;
                return Type.BOOL;
            }
            case "and", "or" -> {
                if (lt.is(Type.K.BOOL) && rt.is(Type.K.BOOL)) return Type.BOOL;
                throw err(at, "'" + op + "' применим только к bool, а получены " + lt + " и " + rt);
            }
            default -> throw err(at, "неизвестный оператор " + op);
        }
    }

    private static Type num(Type a, Type b, Binary bin) {
        Type t = (a.is(Type.K.DOUBLE) || b.is(Type.K.DOUBLE)) ? Type.DOUBLE : Type.INT;
        if (bin != null) bin.operandType = t;
        return t;
    }

    private Type call(Call c) {
        if (BUILTINS.contains(c.name)) {
            c.builtin = c.name;
            List<Type> ts = new ArrayList<>();
            for (Expr a : c.args) {
                Type t = expr(a);
                if (t.is(Type.K.VOID)) throw err(a, "void нельзя передать как аргумент");
                ts.add(t);
            }
            switch (c.name) {
                case "print" -> {
                    if (ts.size() > 1) throw err(c, "print принимает 0 или 1 аргумент (склейте строки через +)");
                    return Type.VOID;
                }
                case "input" -> {
                    if (!ts.isEmpty()) throw err(c, "input() не принимает аргументов");
                    return Type.STRING;
                }
                case "str" -> {
                    if (ts.size() != 1) throw err(c, "str ожидает 1 аргумент");
                    return Type.STRING;
                }
                case "toInt", "toDouble" -> {
                    if (ts.size() != 1) throw err(c, c.name + " ожидает 1 аргумент");
                    Type t = ts.get(0);
                    if (!t.isNum() && !t.is(Type.K.STRING)) throw err(c.args.get(0), c.name + " принимает число или строку, а получен " + t);
                    return c.name.equals("toInt") ? Type.INT : Type.DOUBLE;
                }
                default -> throw err(c, "неизвестная встроенная функция");
            }
        }
        if (!isStatic) {
            MethodInfo m = curClass.findMethod(c.name);
            if (m != null) {
                c.target = m;
                c.implicitThis = true;
                args(c.args, m.params, c, "метод " + c.name);
                return m.ret;
            }
        }
        MethodInfo f = funcs.get(c.name);
        if (f == null) {
            if (classes.containsKey(c.name)) throw err(c, "объект создаётся через new: new " + c.name + "(...)");
            throw err(c, "неизвестная функция '" + c.name + "'");
        }
        c.target = f;
        args(c.args, f.params, c, "функция " + c.name);
        return f.ret;
    }

    private Type methodCall(MethodCall mc) {
        Type tt = expr(mc.target);
        if (tt.is(Type.K.CLASS)) {
            ClassInfo ci = classes.get(tt.cls);
            MethodInfo m = ci.findMethod(mc.name);
            if (m == null) throw err(mc, "у класса " + ci.name + " нет метода '" + mc.name + "'");
            mc.method = m;
            args(mc.args, m.params, mc, "метод " + mc.name);
            return m.ret;
        }
        if (tt.is(Type.K.STRING)) {
            List<Type> ats = new ArrayList<>();
            for (Expr a : mc.args) ats.add(expr(a));
            return stringMethod(mc, ats);
        }
        throw err(mc, "у типа " + tt + " нет метода '" + mc.name + "'");
    }

    // ---------- методы java.lang.String через рефлексию ----------

    private static Type mapJava(Class<?> c) {
        if (c == int.class) return Type.INT;
        if (c == double.class) return Type.DOUBLE;
        if (c == boolean.class) return Type.BOOL;
        if (c == void.class) return Type.VOID;
        if (c == String.class || c == char.class) return Type.STRING;
        if (c.isArray()) {
            Type e = mapJava(c.getComponentType());
            return (e == null || c.getComponentType() == char.class) ? null : Type.arr(e);
        }
        return null;
    }

    /** -1 = не подходит, иначе чем больше, тем точнее. */
    private static int score(Class<?> p, Type a) {
        if (p == int.class) return a.is(Type.K.INT) ? 3 : -1;
        if (p == double.class) return a.is(Type.K.DOUBLE) ? 3 : a.is(Type.K.INT) ? 1 : -1;
        if (p == boolean.class) return a.is(Type.K.BOOL) ? 3 : -1;
        if (p == String.class) return a.is(Type.K.STRING) || a.is(Type.K.NULL) ? 3 : -1;
        if (p == CharSequence.class) return a.is(Type.K.STRING) ? 2 : -1;
        if (p == Object.class) return a.isRef() ? 1 : -1;
        return -1;
    }

    private Type stringMethod(MethodCall mc, List<Type> ats) {
        Method best = null;
        int bestScore = -1;
        for (Method m : String.class.getMethods()) {
            if (Modifier.isStatic(m.getModifiers()) || !m.getName().equals(mc.name)) continue;
            Class<?>[] ps = m.getParameterTypes();
            if (ps.length != ats.size() || mapJava(m.getReturnType()) == null) continue;
            int total = 0;
            for (int i = 0; i < ps.length && total >= 0; i++) {
                int s = score(ps[i], ats.get(i));
                total = s < 0 ? -1 : total + s;
            }
            if (total > bestScore) { bestScore = total; best = m; }
        }
        if (best == null) throw err(mc, "у string нет подходящего метода '" + mc.name + "' с " + ats.size() + " арг.");
        mc.javaMethod = best;
        mc.javaParams = new ArrayList<>();
        for (Class<?> p : best.getParameterTypes()) {
            mc.javaParams.add(p == int.class ? Type.INT : p == double.class ? Type.DOUBLE : p == boolean.class ? Type.BOOL : null);
        }
        mc.charToString = best.getReturnType() == char.class;
        return mapJava(best.getReturnType());
    }
}
