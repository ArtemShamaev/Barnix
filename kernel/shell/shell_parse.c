#include "shell_parse.h"

enum { END, WORD, PIPE, INPUT, OUTPUT, APPEND, ERROR };
typedef struct {
    const char *cursor, *error;
    ShellPlan *plan;
    unsigned int used;
    const char *word;
} Lexer;

static int space(char c) { return c == ' ' || c == '\t'; }
static int operator(char c)
{ return c == '|' || c == '<' || c == '>' || c == '&' || c == ';' || c == '(' || c == ')'; }
static int emit(Lexer *lexer, char c)
{
    if (lexer->used == sizeof(lexer->plan->storage)) {
        lexer->error = "command is too long";
        return 0;
    }
    lexer->plan->storage[lexer->used++] = c;
    return 1;
}
static int next(Lexer *lexer)
{
    const char *p = lexer->cursor;
    while (space(*p)) p++;
    if (!*p) return END;
    if (operator(*p)) {
        char c = *p++;
        int token = c == '|' ? PIPE : c == '<' ? INPUT : OUTPUT;
        if (c == '>' && *p == '>') { p++; token = APPEND; }
        else if ((c != '|' && c != '<' && c != '>') || *p == c) {
            lexer->error = "unsupported shell operator";
            return ERROR;
        }
        lexer->cursor = p;
        return token;
    }
    lexer->word = lexer->plan->storage + lexer->used;
    int quote = 0, number = 1;
    while (*p && (quote || (!space(*p) && !operator(*p)))) {
        char c = *p++;
        if (!quote && (c == '\'' || c == '"')) { quote = c; number = 0; continue; }
        if (quote && c == quote) { quote = 0; continue; }
        if (c == '\\' && quote != '\'') {
            number = 0;
            if (!*p) { lexer->error = "unfinished escape"; return ERROR; }
            /* Inside double quotes, preserve a backslash before an ordinary
             * character. Expansion is absent; dollar/backtick stay literal. */
            if (quote == '"' && *p != '"' && *p != '\\' && *p != '$' && *p != '`') {
                if (!emit(lexer, c)) return ERROR;
                continue;
            }
            c = *p++;
        }
        if (c < '0' || c > '9') number = 0;
        if (!emit(lexer, c)) return ERROR;
    }
    if (quote) { lexer->error = "unclosed quote"; return ERROR; }
    if (number && (*p == '<' || *p == '>')) {
        lexer->error = "descriptor redirection is unsupported";
        return ERROR;
    }
    if (!emit(lexer, 0)) return ERROR;
    lexer->cursor = p;
    return WORD;
}
const char *shell_parse(const char *line, ShellPlan *plan)
{
    *plan = (ShellPlan){0};
    unsigned int length = 0;
    while (line[length]) {
        if (line[length] == '\n' || line[length] == '\r') return "multiline commands are unsupported";
        if (++length == SHELL_LINE_MAX) return "command is too long";
    }
    Lexer lexer = {line, 0, plan, 0, 0};
    int stage = 0, token;
    while ((token = next(&lexer)) != END) {
        ShellCommand *command = &plan->commands[stage];
        if (token == ERROR) return lexer.error;
        if (token == WORD) {
            if (command->argc == SHELL_ARGS_MAX) return "too many arguments";
            command->argv[command->argc++] = lexer.word;
        } else if (token == PIPE) {
            if (!command->argc) return "missing command before pipe";
            if (stage + 1 == SHELL_STAGES_MAX) return "at most two commands per pipeline";
            stage++; plan->compound = 1;
        } else {
            if (command->redirect_count == SHELL_REDIRECTS_MAX) return "too many redirections";
            int target = next(&lexer);
            if (target == ERROR) return lexer.error;
            if (target != WORD) return "missing redirection path";
            ShellRedirect *redirect = &command->redirects[command->redirect_count++];
            redirect->kind = token == INPUT ? SHELL_INPUT : token == OUTPUT ? SHELL_OUTPUT : SHELL_APPEND;
            redirect->path = lexer.word;
            plan->compound = 1;
        }
    }
    if (!plan->commands[0].argc && !plan->compound) return 0;
    for (int i = 0; i <= stage; i++) {
        if (!plan->commands[i].argc) return "missing command";
        if (!plan->commands[i].argv[0][0]) return "empty command name";
    }
    plan->count = stage + 1;
    return 0;
}
