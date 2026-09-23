#include "lang.h"
#include "barnix.h"
#include "elf.h"
#include "linux_exec.h"
#include "linux_task.h"
#include "shell_parse.h"

#define MAX_CMD SHELL_LINE_MAX
_Static_assert(SHELL_STAGES_MAX + 1 <= LINUX_TASK_LIMIT, "pipeline runner needs a parent task slot");

static char cmd[MAX_CMD];
static ShellPlan plan;
extern const unsigned char bssh_runner_start[], bssh_runner_end[];

static void exec_cmd(void)
{
    const char *parse_error = shell_parse(cmd, &plan);
    if (parse_error) { print(RED, "bssh: "); println(RED, parse_error); return; }
    if (!plan.count) return;
    int argc = plan.commands[0].argc;
    const char *const *argv = plan.commands[0].argv;
    if (plan.compound) {
        const char *args[] = {"bssh-run", cmd};
        int status;
        int error = linux_run_image(bssh_runner_start, bssh_runner_end - bssh_runner_start, 2, args, &status);
        console_finish_line();
        print(error ? RED : GREEN, error ? "Linux load error: " : tr("program exited: "));
        print_int(error ? RED : GREEN, error ? error : status);
        println(WHITE, "");
        return;
    }
    if (!strcmp(argv[0], "linux")) {
        if (argc < 2) { println(RED, "usage: linux /path/program [args]"); return; }
        int status;
        int error = linux_run(argv[1], argc - 1, (const char *const *)(argv + 1), &status);
        console_finish_line();
        print(error ? RED : GREEN, error ? "Linux load error: " : "Linux program exited: ");
        print_int(error ? RED : GREEN, error ? error : status);
        println(WHITE, "");
        return;
    }
    char path[MAX_CMD + 5];
    const char *name = argv[0];
    if (!strchr(name, '/')) {
        strcpy(path, "/bin/"); strcat(path, name); name = path;
    }
    int status;
    if (elf_run(name, argc, (const char *const *)argv, &status) == 0) {
        console_finish_line();
        print(GREEN, tr("program exited: ")); print_int(GREEN, status); println(GREEN, "");
    }
}

/* =========================================================
   main shell loop
   ========================================================= */

void bssh_main(void)
{
    banner(tr("Barnix Shell v0.3"));

    while (1)
    {
        print(CYAN, "bssh> ");

        input(cmd, MAX_CMD, "");

        exec_cmd();
    }
}
