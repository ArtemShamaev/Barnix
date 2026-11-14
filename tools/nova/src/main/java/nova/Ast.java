package nova;

import java.util.ArrayList;
import java.util.List;

/** Синтаксическое дерево. Поля «resolved» заполняет Checker, их читает CodeGen. */
final class Ast {
    private Ast() {}

    abstract static class Node {
        int line, col;
    }

    static <T extends Node> T pos(T n, Token t) {
        n.line = t.line;
        n.col = t.col;
        return n;
    }

    static final class TypeRef extends Node {
        final String name;
        final int dims;
        TypeRef(String name, int dims) { this.name = name; this.dims = dims; }
    }

    static final class Program {
        final List<ClassDecl> classes = new ArrayList<>();
        final List<FuncDecl> funcs = new ArrayList<>();
    }

    static final class ClassDecl extends Node {
        String name, superName;
        final List<FieldDecl> fields = new ArrayList<>();
        final List<FuncDecl> methods = new ArrayList<>();
        FuncDecl init;
    }

    static final class FieldDecl extends Node {
        TypeRef type;
        String name;
        Expr init;
    }

    static final class Param extends Node {
        final TypeRef type;
        final String name;
        Checker.Local local;
        Param(TypeRef type, String name) { this.type = type; this.name = name; }
    }

    static final class FuncDecl extends Node {
        TypeRef ret;
        String name;
        final List<Param> params = new ArrayList<>();
        List<Stmt> body;
        boolean isInit;
        Checker.MethodInfo info;
    }

    // ---------- операторы ----------

    abstract static class Stmt extends Node {}

    static final class VarDecl extends Stmt {
        TypeRef declType; // null для var
        String name;
        Expr init;        // может быть null
        Checker.Local local;
    }

    static final class Assign extends Stmt {
        Expr target;
        String op;        // "=", "+=", ...
        Expr value;
    }

    static final class If extends Stmt {
        Expr cond;
        List<Stmt> then;
        List<Stmt> els;   // null, если нет else
    }

    static final class While extends Stmt {
        Expr cond;
        List<Stmt> body;
    }

    static final class For extends Stmt {
        String var;
        Expr from;
        Expr to;          // null => for x in массив
        List<Stmt> body;
        Checker.Local local, hidden1, hidden2;
    }

    static final class Return extends Stmt { Expr value; }
    static final class Break extends Stmt {}
    static final class Continue extends Stmt {}
    static final class Pass extends Stmt {}

    static final class ExprStmt extends Stmt { Expr expr; }

    static final class SuperCall extends Stmt {
        final List<Expr> args = new ArrayList<>();
        Checker.MethodInfo ctor; // null => конструктор без параметров
    }

    // ---------- выражения ----------

    abstract static class Expr extends Node {
        Type type;
    }

    static final class IntLit extends Expr { int v; }
    static final class DoubleLit extends Expr { double v; }
    static final class StrLit extends Expr { String v; }
    static final class BoolLit extends Expr { boolean v; }
    static final class NullLit extends Expr {}
    static final class This extends Expr {}

    static final class Name extends Expr {
        String name;
        Checker.Local local;
        Checker.FieldInfo field; // неявный this.field
    }

    static final class Binary extends Expr {
        String op;
        Expr l, r;
        Type operandType;
    }

    static final class Unary extends Expr {
        String op;
        Expr e;
    }

    static final class Call extends Expr {
        String name;
        final List<Expr> args = new ArrayList<>();
        Checker.MethodInfo target;
        boolean implicitThis;
        String builtin;
    }

    static final class MethodCall extends Expr {
        Expr target;
        String name;
        final List<Expr> args = new ArrayList<>();
        Checker.MethodInfo method;          // метод класса Nova
        java.lang.reflect.Method javaMethod; // метод java.lang.String
        List<Type> javaParams;
        boolean charToString;
    }

    static final class SuperMethodCall extends Expr {
        String name;
        final List<Expr> args = new ArrayList<>();
        Checker.MethodInfo method;
        String superInternal;
    }

    static final class Field extends Expr {
        Expr target;
        String name;
        Checker.FieldInfo field;
        boolean arrayLength;
    }

    static final class Index extends Expr {
        Expr target, index;
    }

    static final class New extends Expr {
        String cls;
        final List<Expr> args = new ArrayList<>();
        Checker.ClassInfo info;
    }

    static final class NewArray extends Expr {
        TypeRef elem;
        Expr size;
        Type elemType;
    }

    static final class ArrayLit extends Expr {
        final List<Expr> elems = new ArrayList<>();
        Type elemType;
    }
}
