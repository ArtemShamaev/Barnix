#include "syscall.h"
#include "../shell_parse.h"

static ShellPlan plan;

static int error(const char *name, const char *message, int status)
{
    text(2, "bssh: ");
    if (name) { text(2, name); text(2, ": "); }
    text(2, message); text(2, "\n");
    return status;
}
static int redirect_fd(int fd, int target)
{
    if (fd < 0) return -1;
    int result = CALL(dup2, fd, target, 0);
    if (fd != target) CALL(close, fd, 0, 0);
    return result == target ? 0 : -1;
}
static int run_command(const ShellCommand *command)
{
    const char *const *args = command->argv;
    int explicit_linux = equal(args[0], "linux");
    if (explicit_linux && command->argc < 2) return error(0, "usage: linux /path/program [args]", 2);
    if (explicit_linux) args++;
    const char *name = args[0], *path = name;
    char resolved[SHELL_LINE_MAX + 5];
    int slash = 0;
    for (unsigned int i = 0; name[i]; i++) if (name[i] == '/') slash = 1;
    if (!slash && !explicit_linux) {
        const char prefix[] = "/bin/";
        unsigned int i = 0;
        for (; i < 5; i++) resolved[i] = prefix[i];
        for (unsigned int j = 0;; j++, i++) { resolved[i] = name[j]; if (!name[j]) break; }
        path = resolved;
    }
    /* Linux itself may accept standalone-tagged ELF. Never enter a Barnix
     * function-pointer program from this Linux-only runner, including on Linux. */
    int fd = CALL(open, path, 0, 0);
    int lookup_error = fd < 0 ? fd : 0;
    unsigned char ident[16];
    int got = 0;
    if (fd >= 0) {
        got = CALL(read, fd, ident, sizeof(ident));
        CALL(close, fd, 0, 0);
    }
    if (got == 16 && ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' &&
        ident[3] == 'F' && ident[7] == 255)
        return error(name, "pipelines and redirections require Linux programs", 126);
    /* Pipe wiring precedes explicit redirections; redirections are applied
     * left to right, including creation/truncation of overwritten targets. */
    for (int i = 0; i < command->redirect_count; i++) {
        const ShellRedirect *r = &command->redirects[i];
        int input = r->kind == SHELL_INPUT;
        unsigned int flags = input ? 0 : 1 | 64 | (r->kind == SHELL_APPEND ? 1024 : 512);
        fd = CALL(open, r->path, flags, 0644);
        if (fd < 0) return error(r->path, "cannot open redirection", 1);
        if (redirect_fd(fd, input ? 0 : 1)) return error(r->path, "cannot redirect descriptor", 1);
    }
    if (lookup_error)
        return error(name, lookup_error == -2 ? "command not found" : "cannot open executable", lookup_error == -2 ? 127 : 126);
    int result = CALL(execve, path, args, 0);
    return error(name, result == -2 ? "command not found" : "cannot execute Linux program", result == -2 ? 127 : 126);
}
static int wait_for(int child)
{
    int status = 0, result;
    do { result = CALL(waitpid, child, &status, 0); } while (result == -4);
    if (result != child) return error(0, "cannot wait for pipeline", 125);
    return (status & 127) ? 128 + (status & 127) : (status >> 8) & 255;
}
int main(int argc, const char *const *argv)
{
    if (argc != 2) return error(0, "expected one command line", 2);
    const char *message = shell_parse(argv[1], &plan);
    if (message) return error(0, message, 2);
    if (!plan.count) return 0;
    if (plan.count == 1) return run_command(&plan.commands[0]);
    int pipe[2];
    if (CALL(pipe, pipe, 0, 0) < 0) return error(0, "cannot create pipe", 125);
    int left = CALL(fork, 0, 0, 0);
    if (!left) {
        CALL(close, pipe[0], 0, 0);
        if (redirect_fd(pipe[1], 1)) return error(0, "cannot connect pipe output", 125);
        return run_command(&plan.commands[0]);
    }
    if (left < 0) {
        CALL(close, pipe[0], 0, 0); CALL(close, pipe[1], 0, 0);
        return error(0, "cannot fork pipeline", 125);
    }
    CALL(close, pipe[1], 0, 0);
    int right = CALL(fork, 0, 0, 0);
    if (!right) {
        if (redirect_fd(pipe[0], 0)) return error(0, "cannot connect pipe input", 125);
        return run_command(&plan.commands[1]);
    }
    CALL(close, pipe[0], 0, 0);
    (void)wait_for(left);
    if (right < 0) return error(0, "cannot fork pipeline", 125);
    return wait_for(right);
}
