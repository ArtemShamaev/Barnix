package nova;

import static nova.Ast.*;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** Рекурсивный спуск: токены -> AST. */
final class Parser {
    private static final Set<String> PRIM = Set.of("int", "double", "bool", "string", "void");
    private static final Set<String> ASSIGN_OPS = Set.of("=", "+=", "-=", "*=", "/=", "%=");

    private final List<Token> t;
    private int p = 0;

    Parser(List<Token> tokens) { this.t = tokens; }

    // ---------- утилиты ----------

    private Token peek() { return t.get(p); }
    private Token peek(int k) { return t.get(Math.min(p + k, t.size() - 1)); }
    private Token next() { Token tk = t.get(p); if (tk.kind != Token.K.EOF) p++; return tk; }
    private boolean at(String s) { return peek().is(s); }
    private boolean atKind(Token.K k) { return peek().kind == k; }

    private boolean accept(String s) {
        if (at(s)) { p++; return true; }
        return false;
    }

    private static CompileError err(Token tk, String msg) { return new CompileError(tk.line, tk.col, msg); }

    private Token expect(String s) {
        if (!at(s)) throw err(peek(), "ожидалось '" + s + "', а найдено " + peek());
        return next();
    }

    private Token expectIdent(String what) {
        if (!atKind(Token.K.IDENT)) throw err(peek(), "ожидалось " + what + ", а найдено " + peek());
        return next();
    }

    private void expectNewline() {
        if (!atKind(Token.K.NEWLINE)) throw err(peek(), "ожидался конец строки, а найдено " + peek());
        next();
    }

    private boolean isTypeStart() {
        Token tk = peek();
        return (tk.kind == Token.K.KW && PRIM.contains(tk.text)) || tk.kind == Token.K.IDENT;
    }

    // ---------- верхний уровень ----------

    Program parseProgram() {
        Program prog = new Program();
        while (!atKind(Token.K.EOF)) {
            if (atKind(Token.K.NEWLINE)) { next(); continue; }
            if (atKind(Token.K.INDENT)) throw err(peek(), "неожиданный отступ");
            if (at("class")) prog.classes.add(parseClass());
            else if (isTypeStart()) prog.funcs.add(parseFunc(parseType()));
            else throw err(peek(), "ожидалось объявление функции или класса, а найдено " + peek());
        }
        return prog;
    }

    private TypeRef parseType() {
        Token tk = peek();
        if (!isTypeStart()) throw err(tk, "ожидался тип, а найдено " + tk);
        next();
        int dims = 0;
        while (at("[") && peek(1).is("]")) { p += 2; dims++; }
        return pos(new TypeRef(tk.text, dims), tk);
    }

    private ClassDecl parseClass() {
        Token kw = expect("class");
        ClassDecl cd = pos(new ClassDecl(), kw);
        cd.name = expectIdent("имя класса").text;
        if (accept("(")) {
            cd.superName = expectIdent("имя базового класса").text;
            expect(")");
        }
        expect(":");
        expectNewline();
        if (!atKind(Token.K.INDENT)) throw err(peek(), "тело класса должно быть с отступом");
        next();
        while (!atKind(Token.K.DEDENT) && !atKind(Token.K.EOF)) {
            if (atKind(Token.K.NEWLINE)) { next(); continue; }
            if (at("pass")) { next(); expectNewline(); continue; }
            if (at("init")) {
                Token it = next();
                if (cd.init != null) throw err(it, "у класса может быть только один init");
                FuncDecl f = pos(new FuncDecl(), it);
                f.isInit = true;
                f.name = "init";
                f.ret = pos(new TypeRef("void", 0), it);
                parseFuncRest(f);
                cd.init = f;
                continue;
            }
            TypeRef type = parseType();
            Token name = expectIdent("имя поля или метода");
            if (at("(")) {
                FuncDecl f = pos(new FuncDecl(), name);
                f.ret = type;
                f.name = name.text;
                parseFuncRest(f);
                cd.methods.add(f);
            } else {
                FieldDecl fd = pos(new FieldDecl(), name);
                fd.type = type;
                fd.name = name.text;
                if (accept("=")) fd.init = parseExpr();
                expectNewline();
                cd.fields.add(fd);
            }
        }
        next(); // DEDENT
        return cd;
    }

    private FuncDecl parseFunc(TypeRef ret) {
        Token name = expectIdent("имя функции");
        FuncDecl f = pos(new FuncDecl(), name);
        f.ret = ret;
        f.name = name.text;
        parseFuncRest(f);
        return f;
    }

    private void parseFuncRest(FuncDecl f) {
        expect("(");
        if (!at(")")) {
            do {
                TypeRef pt = parseType();
                Token pn = expectIdent("имя параметра");
                f.params.add(pos(new Param(pt, pn.text), pn));
            } while (accept(","));
        }
        expect(")");
        expect(":");
        f.body = parseBlock();
    }

    private List<Stmt> parseBlock() {
        List<Stmt> list = new ArrayList<>();
        if (atKind(Token.K.NEWLINE)) {
            next();
            if (!atKind(Token.K.INDENT)) throw err(peek(), "ожидался блок с отступом");
            next();
            while (!atKind(Token.K.DEDENT) && !atKind(Token.K.EOF)) {
                if (atKind(Token.K.NEWLINE)) { next(); continue; }
                list.add(parseStmt());
            }
            next(); // DEDENT
        } else {
            // однострочный блок: if x: return 1
            list.add(parseSimple());
            expectNewline();
        }
        return list;
    }

    // ---------- операторы ----------

    private Stmt parseStmt() {
        if (atKind(Token.K.INDENT)) throw err(peek(), "неожиданный отступ");
        if (at("if")) return parseIf();
        if (at("while")) {
            Token kw = next();
            While w = pos(new While(), kw);
            w.cond = parseExpr();
            expect(":");
            w.body = parseBlock();
            return w;
        }
        if (at("for")) {
            Token kw = next();
            For f = pos(new For(), kw);
            f.var = expectIdent("имя переменной цикла").text;
            expect("in");
            f.from = parseExpr();
            if (accept("..")) f.to = parseExpr();
            expect(":");
            f.body = parseBlock();
            return f;
        }
        if (at("else") || at("elif")) throw err(peek(), "'" + peek().text + "' без соответствующего if");
        Stmt s = parseSimple();
        expectNewline();
        return s;
    }

    private If parseIf() {
        Token kw = next(); // if / elif
        If s = pos(new If(), kw);
        s.cond = parseExpr();
        expect(":");
        s.then = parseBlock();
        if (at("elif")) {
            s.els = new ArrayList<>(List.of(parseIf()));
        } else if (at("else")) {
            next();
            expect(":");
            s.els = parseBlock();
        }
        return s;
    }

    private boolean isDeclStart() {
        Token a = peek();
        if (a.kind == Token.K.KW && PRIM.contains(a.text) && !a.text.equals("void")) return true;
        if (a.kind != Token.K.IDENT) return false;
        Token b = peek(1);
        if (b.kind == Token.K.IDENT) return true;
        return b.is("[") && peek(2).is("]");
    }

    private Stmt parseSimple() {
        Token tk = peek();
        switch (tk.text) {
            case "return" -> {
                if (tk.kind != Token.K.KW) break;
                next();
                Return r = pos(new Return(), tk);
                if (!atKind(Token.K.NEWLINE)) r.value = parseExpr();
                return r;
            }
            case "break" -> { if (tk.kind == Token.K.KW) { next(); return pos(new Break(), tk); } }
            case "continue" -> { if (tk.kind == Token.K.KW) { next(); return pos(new Continue(), tk); } }
            case "pass" -> { if (tk.kind == Token.K.KW) { next(); return pos(new Pass(), tk); } }
            case "var" -> {
                if (tk.kind != Token.K.KW) break;
                next();
                VarDecl d = pos(new VarDecl(), tk);
                Token name = expectIdent("имя переменной");
                d.name = name.text;
                d.line = name.line; d.col = name.col;
                expect("=");
                d.init = parseExpr();
                return d;
            }
            case "super" -> {
                if (tk.kind == Token.K.KW && peek(1).is("(")) {
                    next();
                    SuperCall sc = pos(new SuperCall(), tk);
                    parseArgs(sc.args);
                    return sc;
                }
            }
            default -> {}
        }
        if (isDeclStart()) {
            VarDecl d = pos(new VarDecl(), tk);
            d.declType = parseType();
            Token name = expectIdent("имя переменной");
            d.name = name.text;
            if (accept("=")) d.init = parseExpr();
            return d;
        }
        Expr e = parseExpr();
        if (peek().kind == Token.K.OP && ASSIGN_OPS.contains(peek().text)) {
            Token op = next();
            if (!(e instanceof Name || e instanceof Field || e instanceof Index))
                throw err(op, "слева от '" + op.text + "' должна быть переменная, поле или элемент массива");
            Assign a = pos(new Assign(), op);
            a.target = e;
            a.op = op.text;
            a.value = parseExpr();
            return a;
        }
        ExprStmt es = new ExprStmt();
        es.line = e.line; es.col = e.col;
        es.expr = e;
        return es;
    }

    // ---------- выражения ----------

    Expr parseExpr() { return parseOr(); }

    private Expr bin(Token op, Expr l, Expr r) {
        Binary b = pos(new Binary(), op);
        b.op = op.text;
        b.l = l;
        b.r = r;
        return b;
    }

    private Expr parseOr() {
        Expr e = parseAnd();
        while (at("or")) { Token op = next(); e = bin(op, e, parseAnd()); }
        return e;
    }

    private Expr parseAnd() {
        Expr e = parseNot();
        while (at("and")) { Token op = next(); e = bin(op, e, parseNot()); }
        return e;
    }

    private Expr parseNot() {
        if (at("not")) {
            Token op = next();
            Unary u = pos(new Unary(), op);
            u.op = "not";
            u.e = parseNot();
            return u;
        }
        return parseCmp();
    }

    private static final Set<String> CMP = Set.of("==", "!=", "<", "<=", ">", ">=");

    private Expr parseCmp() {
        Expr e = parseAdd();
        if (peek().kind == Token.K.OP && CMP.contains(peek().text)) {
            Token op = next();
            e = bin(op, e, parseAdd());
            if (peek().kind == Token.K.OP && CMP.contains(peek().text))
                throw err(peek(), "цепочки сравнений не поддерживаются: используйте 'and'");
        }
        return e;
    }

    private Expr parseAdd() {
        Expr e = parseMul();
        while (at("+") || at("-")) { Token op = next(); e = bin(op, e, parseMul()); }
        return e;
    }

    private Expr parseMul() {
        Expr e = parseUnary();
        while (at("*") || at("/") || at("%")) { Token op = next(); e = bin(op, e, parseUnary()); }
        return e;
    }

    private Expr parseUnary() {
        if (at("-")) {
            Token op = next();
            Expr inner = parseUnary();
            if (inner instanceof IntLit il) { il.v = -il.v; il.line = op.line; il.col = op.col; return il; }
            if (inner instanceof DoubleLit dl) { dl.v = -dl.v; dl.line = op.line; dl.col = op.col; return dl; }
            Unary u = pos(new Unary(), op);
            u.op = "-";
            u.e = inner;
            return u;
        }
        return parsePostfix(parsePrimary());
    }

    private Expr parsePostfix(Expr e) {
        while (true) {
            if (at(".")) {
                next();
                Token name = expectIdent("имя поля или метода");
                if (at("(")) {
                    MethodCall mc = pos(new MethodCall(), name);
                    mc.target = e;
                    mc.name = name.text;
                    parseArgs(mc.args);
                    e = mc;
                } else {
                    Field f = pos(new Field(), name);
                    f.target = e;
                    f.name = name.text;
                    e = f;
                }
            } else if (at("[")) {
                Token br = next();
                Index ix = pos(new Index(), br);
                ix.target = e;
                ix.index = parseExpr();
                expect("]");
                e = ix;
            } else if (at("(")) {
                throw err(peek(), "это выражение нельзя вызвать как функцию");
            } else {
                return e;
            }
        }
    }

    private void parseArgs(List<Expr> into) {
        expect("(");
        if (!at(")")) {
            do { into.add(parseExpr()); } while (accept(","));
        }
        expect(")");
    }

    private Expr parsePrimary() {
        Token tk = peek();
        switch (tk.kind) {
            case INT -> { next(); IntLit l = pos(new IntLit(), tk); l.v = (Integer) tk.value; return l; }
            case DOUBLE -> { next(); DoubleLit l = pos(new DoubleLit(), tk); l.v = (Double) tk.value; return l; }
            case STRING -> { next(); StrLit l = pos(new StrLit(), tk); l.v = tk.text; return l; }
            case IDENT -> {
                next();
                if (at("(")) {
                    Call c = pos(new Call(), tk);
                    c.name = tk.text;
                    parseArgs(c.args);
                    return c;
                }
                Name n = pos(new Name(), tk);
                n.name = tk.text;
                return n;
            }
            default -> {}
        }
        if (tk.is("true") || tk.is("false")) {
            next();
            BoolLit b = pos(new BoolLit(), tk);
            b.v = tk.text.equals("true");
            return b;
        }
        if (tk.is("null")) { next(); return pos(new NullLit(), tk); }
        if (tk.is("this")) { next(); return pos(new This(), tk); }
        if (tk.is("super")) {
            next();
            expect(".");
            Token name = expectIdent("имя метода");
            SuperMethodCall sc = pos(new SuperMethodCall(), name);
            sc.name = name.text;
            if (!at("(")) throw err(peek(), "после super.имя ожидается вызов метода");
            parseArgs(sc.args);
            return sc;
        }
        if (tk.is("(")) {
            next();
            Expr e = parseExpr();
            expect(")");
            return e;
        }
        if (tk.is("[")) {
            next();
            ArrayLit a = pos(new ArrayLit(), tk);
            if (!at("]")) {
                do { a.elems.add(parseExpr()); } while (accept(","));
            }
            expect("]");
            return a;
        }
        if (tk.is("new")) {
            next();
            Token base = peek();
            if (!isTypeStart()) throw err(base, "после new ожидался тип");
            next();
            if (at("(")) {
                if (base.kind != Token.K.IDENT) throw err(base, "new " + base.text + "(...) невозможен: это не класс");
                New n = pos(new New(), base);
                n.cls = base.text;
                parseArgs(n.args);
                return n;
            }
            if (at("[")) {
                next();
                NewArray na = pos(new NewArray(), base);
                na.size = parseExpr();
                expect("]");
                int dims = 0;
                while (at("[") && peek(1).is("]")) { p += 2; dims++; }
                na.elem = pos(new TypeRef(base.text, dims), base);
                return na;
            }
            throw err(peek(), "после new " + base.text + " ожидалось '(' или '['");
        }
        throw err(tk, "ожидалось выражение, а найдено " + tk);
    }
}
