using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System.Text.Json;

// Explicit native subset: unsupported syntax is an error, never silently discarded.
static class Compiler
{
    static Exception Unsupported(SyntaxNode n) => new Exception($"{n.GetLocation().GetLineSpan()}: unsupported native C# syntax: {n.Kind()}");
    static string Type(TypeSyntax t) => t.ToString() switch {
        "void" => "void", "int" => "int", "uint" => "unsigned int", "bool" => "bool",
        "string" => "const char*", "MouseState" => "MouseState", "GraphicEvent" => "GraphicEvent", "var" => "auto",
        _ => throw Unsupported(t)
    };
    static SemanticModel Model;
    static string Expr(ExpressionSyntax e)
    {
        var expressionType = Model.GetTypeInfo(e).Type;
        if (Model.GetTypeInfo(e).ConvertedType?.SpecialType is SpecialType.System_Int64 or SpecialType.System_UInt64) throw Unsupported(e);
        if (expressionType?.SpecialType is SpecialType.System_Int64 or SpecialType.System_UInt64 or SpecialType.System_Double or SpecialType.System_Single or SpecialType.System_Decimal) throw Unsupported(e);
        if (e is BinaryExpressionSyntax binary && (Model.GetTypeInfo(binary.Left).Type?.SpecialType == SpecialType.System_String || Model.GetTypeInfo(binary.Right).Type?.SpecialType == SpecialType.System_String)) throw Unsupported(e);
        return e switch {
        LiteralExpressionSyntax l when l.IsKind(SyntaxKind.StringLiteralExpression) => JsonSerializer.Serialize(l.Token.ValueText, new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping }),
        LiteralExpressionSyntax l when l.IsKind(SyntaxKind.NumericLiteralExpression) && (l.Token.Value is int || l.Token.Value is uint) => Convert.ToString(l.Token.Value, System.Globalization.CultureInfo.InvariantCulture) + (l.Token.Value is uint ? "U" : ""),
        LiteralExpressionSyntax l when l.IsKind(SyntaxKind.TrueLiteralExpression) || l.IsKind(SyntaxKind.FalseLiteralExpression) => l.Token.ValueText,
        IdentifierNameSyntax i => i.Identifier.Text,
        ParenthesizedExpressionSyntax p => "(" + Expr(p.Expression) + ")",
        MemberAccessExpressionSyntax m => Expr(m.Expression) + (new[] {"Console", "Input", "Mouse", "Graphics", "Thread", "Environment"}.Contains(m.Expression.ToString()) ? "::" : ".") + m.Name.Identifier.Text,
        InvocationExpressionSyntax i => Call(i),
        BinaryExpressionSyntax b when new[] {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&&", "||", "&", "|", "^"}.Contains(b.OperatorToken.Text) => Binary(b),
        AssignmentExpressionSyntax a => Expr(a.Left) + a.OperatorToken.Text + Expr(a.Right),
        PrefixUnaryExpressionSyntax p when new[] {"!", "-", "+", "~", "++", "--"}.Contains(p.OperatorToken.Text) => p.OperatorToken.Text + Expr(p.Operand),
        PostfixUnaryExpressionSyntax p => Expr(p.Operand) + p.OperatorToken.Text,
        CastExpressionSyntax c => "(" + Type(c.Type) + ")(" + Expr(c.Expression) + ")",
        ConditionalExpressionSyntax c => "(" + Expr(c.Condition) + "?" + Expr(c.WhenTrue) + ":" + Expr(c.WhenFalse) + ")",
        _ => throw Unsupported(e)
    };
    }
    static string Call(InvocationExpressionSyntax call)
    {
        var arguments = call.ArgumentList.Arguments.Select(a => a.RefKindKeyword.RawKind == 0 && a.NameColon == null ? Expr(a.Expression) : throw Unsupported(a)).ToArray();
        return "([&]() { " + string.Join(" ", arguments.Select((a, i) => "auto __bdk_arg_" + i + " = " + a + ";"))
            + " return " + Expr(call.Expression) + "(" + string.Join(",", arguments.Select((a, i) => "__bdk_arg_" + i)) + "); }())";
    }
    static string Binary(BinaryExpressionSyntax b)
    {
        string left = Expr(b.Left), right = Expr(b.Right), op = b.OperatorToken.Text;
        if (op is "&&" or "||") return "(" + left + op + right + ")";
        return "([&]() { auto __bdk_left = " + left + "; auto __bdk_right = " + right + "; return __bdk_left " + op + " __bdk_right; }())";
    }
    static string StringArray(VariableDeclarationSyntax d) {
        if (d.Variables.Count != 1 || d.Variables[0].Initializer?.Value is not ArrayCreationExpressionSyntax a || a.Type.ToString() != "string[]" || a.Initializer == null || a.Initializer.Expressions.Count == 0) throw Unsupported(d);
        // Static storage matches BGL's retained list pointers. Only literal items
        // are accepted, so initialization cannot depend on a vanished stack frame.
        if (a.Initializer.Expressions.Any(e => e is not LiteralExpressionSyntax l || !l.IsKind(SyntaxKind.StringLiteralExpression))) throw Unsupported(d);
        return "static const char *const " + d.Variables[0].Identifier.Text + "[]={" + string.Join(",", a.Initializer.Expressions.Select(Expr)) + "}";
    }
    static string Vars(VariableDeclarationSyntax d) => d.Type.ToString() == "string[]" ? StringArray(d) : Type(d.Type) + " " + string.Join(",", d.Variables.Select(v => v.Identifier.Text + (v.Initializer == null ? "{}" : "=" + Expr(v.Initializer.Value))));
    static string Statement(StatementSyntax s) => s switch {
        BlockSyntax b => "{\n" + string.Join("\n", b.Statements.Select(Statement)) + "\n}",
        LocalDeclarationStatementSyntax l when l.Modifiers.Count == 0 => Vars(l.Declaration) + ";",
        ExpressionStatementSyntax e => Expr(e.Expression) + ";",
        ReturnStatementSyntax r => "return " + (r.Expression == null ? "" : Expr(r.Expression)) + ";",
        IfStatementSyntax i => "if(" + Expr(i.Condition) + ")" + Statement(i.Statement) + (i.Else == null ? "" : "else " + Statement(i.Else.Statement)),
        WhileStatementSyntax w => "while(" + Expr(w.Condition) + ")" + Statement(w.Statement),
        ForStatementSyntax f => "for(" + (f.Declaration == null ? string.Join(",", f.Initializers.Select(Expr)) : Vars(f.Declaration)) + ";" + (f.Condition == null ? "" : Expr(f.Condition)) + ";" + string.Join(",", f.Incrementors.Select(Expr)) + ")" + Statement(f.Statement),
        BreakStatementSyntax => "break;", ContinueStatementSyntax => "continue;", EmptyStatementSyntax => ";",
        _ => throw Unsupported(s)
    };
    static int Main(string[] args)
    {
        try {
            var trees = Directory.GetFiles(args[0], "*.cs", SearchOption.AllDirectories).Order().Select(p => CSharpSyntaxTree.ParseText(File.ReadAllText(p), path:p)).ToArray();
            foreach (var tree in trees) {
                var errors = tree.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
                if (errors.Length != 0) throw new Exception(string.Join("\n", errors.Select(d => d.ToString())));
                if (tree.GetRoot().DescendantTrivia().Any(t => t.IsDirective)) throw new Exception("C# preprocessor directives are not supported");
            }
            using var apiStream = typeof(Compiler).Assembly.GetManifestResourceStream("Compiler.Api.cs.txt")!;
            var api = CSharpSyntaxTree.ParseText(new StreamReader(apiStream).ReadToEnd());
            var compilation = CSharpCompilation.Create("BarnixApp", trees.Append(api),
                new[] { MetadataReference.CreateFromFile(typeof(object).Assembly.Location) },
                new CSharpCompilationOptions(OutputKind.ConsoleApplication, mainTypeName: "Program"));
            var diagnostics = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
            if (diagnostics.Length != 0) throw new Exception(string.Join("\n", diagnostics.Select(d => d.ToString())));
            var methods = new List<MethodDeclarationSyntax>();
            string name = null;
            foreach (var tree in trees) {
                var root = (CompilationUnitSyntax)tree.GetRoot();
                foreach (var token in root.DescendantTokens())
                    if (token.Text.StartsWith("__bdk_") || new[] {"async", "unsafe", "extern", "virtual", "override", "abstract", "volatile", "readonly", "checked", "unchecked"}.Contains(token.Text)) throw new Exception("Unsupported C# keyword: " + token.Text);
                if (root.AttributeLists.Count != 0 || root.Externs.Count != 0) throw Unsupported(root);
                foreach (var u in root.Usings)
                    if (u.Name?.ToString() != "Barnix" || u.Alias != null || u.StaticKeyword.RawKind != 0) throw Unsupported(u);
                foreach (var member in root.Members) {
                    if (member is not ClassDeclarationSyntax c || c.Identifier.Text != "Program" || c.BaseList != null || c.TypeParameterList != null || c.AttributeLists.Count != 0) throw Unsupported(member);
                    foreach (var m in c.Members) {
                        if (m is FieldDeclarationSyntax f && f.Modifiers.Any(SyntaxKind.ConstKeyword) && f.Declaration.Type.ToString() == "string" && f.Declaration.Variables.Count == 1 && f.Declaration.Variables[0].Identifier.Text == "AppName" && f.Declaration.Variables[0].Initializer?.Value is LiteralExpressionSyntax l && l.IsKind(SyntaxKind.StringLiteralExpression) && name == null) name = l.Token.ValueText;
                        else if (m is MethodDeclarationSyntax method && method.Modifiers.Any(SyntaxKind.StaticKeyword) && method.Body != null && method.TypeParameterList == null && method.AttributeLists.Count == 0) methods.Add(method);
                        else throw Unsupported(m);
                    }
                }
            }
            if (name == null || !System.Text.RegularExpressions.Regex.IsMatch(name, @"\A[A-Za-z0-9_][A-Za-z0-9_.-]*\.elf\z")) throw new Exception("Define const string AppName = \"app.elf\" (a filename, not a path)");
            if (name.Length > 23) throw new Exception("Barnix filenames are limited to 23 characters");
            var entries = methods.Where(m => m.Identifier.Text == "Main").ToArray();
            if (entries.Length != 1 || entries[0].ReturnType.ToString() != "int" || entries[0].ParameterList.Parameters.Count != 0) throw new Exception("Exactly one static int Main() is required");
            string Signature(MethodDeclarationSyntax m) => Type(m.ReturnType) + " " + m.Identifier.Text + "(" + string.Join(",", m.ParameterList.Parameters.Select(p => p.Modifiers.Count == 0 && p.Default == null ? Type(p.Type!) + " " + p.Identifier.Text : throw Unsupported(p))) + ")";
            var code = "#define BARNIX_APP_NAME \"" + name + "\"\n#include <barnix_api>\n#include <barnix_graphics>\nusing namespace Barnix;\n[[maybe_unused]] static const char* AppName = BARNIX_APP_NAME;\n";
            code += string.Join("\n", methods.Select(m => Signature(m) + ";"));
            code += string.Join("\n", methods.Select(m => { Model = compilation.GetSemanticModel(m.SyntaxTree); return Signature(m) + Statement(m.Body!); }));
            code += "\nint main() { return Main(); }\n";
            File.WriteAllText(args[1], code);
            return 0;
        } catch (Exception e) { System.Console.Error.WriteLine("BDK C#: " + e.Message); return 1; }
    }
}
