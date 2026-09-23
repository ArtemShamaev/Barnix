#ifndef BARNIX_SHELL_PARSE_H
#define BARNIX_SHELL_PARSE_H

#define SHELL_LINE_MAX 128
#define SHELL_ARGS_MAX 64
#define SHELL_STAGES_MAX 2
#define SHELL_REDIRECTS_MAX 8

enum { SHELL_INPUT, SHELL_OUTPUT, SHELL_APPEND };
typedef struct { int kind; const char *path; } ShellRedirect;
typedef struct {
    int argc, redirect_count;
    const char *argv[SHELL_ARGS_MAX + 1];
    ShellRedirect redirects[SHELL_REDIRECTS_MAX];
} ShellCommand;
typedef struct {
    int count, compound;
    ShellCommand commands[SHELL_STAGES_MAX];
    char storage[SHELL_LINE_MAX];
} ShellPlan;

/* Parse one bounded line without executing anything. Argument/path pointers
 * belong to plan->storage; the input remains unchanged. NULL means success. */
const char *shell_parse(const char *line, ShellPlan *plan);
#endif
