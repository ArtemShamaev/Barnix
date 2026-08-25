#include "barnix.h"
#include "fs.h"

#define MAX_CMD 128
#define MAX_ARGS 8

static char cmd[MAX_CMD];
static char *argv[MAX_ARGS];
/* =========================================================
   split command -> argv
   ========================================================= */

static int parse(char *line)
{
    int argc = 0;

    char *tok = strtok(line, " ");

    while (tok && argc < MAX_ARGS)
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " ");
    }

    return argc;
}

/* =========================================================
   commands
   ========================================================= */

static void cmd_help(void)
{
    banner("Barnix Shell");

    println(WHITE, "help        - show help");
    println(WHITE, "clear       - clear screen");
    println(WHITE, "ls          - list files");
    println(WHITE, "cat <file>  - print file");
    println(WHITE, "touch <f>   - create file");
    println(WHITE, "write txt f - write text");
    println(WHITE, "rm <file>   - remove file");
    println(WHITE, "echo text   - print text");
    println(WHITE, "panic       - crash system");
    println(WHITE, "");
}

static void cmd_echo(int argc)
{
    for (int i = 1; i < argc; i++)
    {
        print(WHITE, argv[i]);
        if (i + 1 < argc)
            print(WHITE, " ");
    }
    println(WHITE, "");
}
static void exec_cmd(int argc)
{
    if (argc == 0)
        return;

    /* help */
    if (strcmp(argv[0], "help") == 0)
    {
        cmd_help();
        return;
    }

    /* clear */
    if (strcmp(argv[0], "clear") == 0)
    {
        clear();
        return;
    }

    /* ls */
    if (strcmp(argv[0], "ls") == 0)
    {
        fs_ls();
        return;
    }

    /* cat */
    if (strcmp(argv[0], "cat") == 0)
    {
        if (argc < 2)
        {
            println(RED, "usage: cat <file>");
            return;
        }

        fs_cat(argv[1]);
        return;
    }

    /* touch */
    if (strcmp(argv[0], "touch") == 0)
    {
        if (argc < 2)
        {
            println(RED, "usage: touch <file>");
            return;
        }

        fs_touch(argv[1]);
        return;
    }

    /* rm */
    if (strcmp(argv[0], "rm") == 0)
    {
        if (argc < 2)
        {
            println(RED, "usage: rm <file>");
            return;
        }

        fs_rm(argv[1]);
        return;
    }
    /* write */
    if (strcmp(argv[0], "write") == 0)
    {
        if (argc < 3)
        {
            println(RED, "usage: write <text> <file>");
            return;
        }

        /* write <text> <file>: text is argv[1], file is argv[2] */
        fs_write(argv[2], argv[1], strlen(argv[1]));
        return;
    }

    /* echo */
    if (strcmp(argv[0], "echo") == 0)
    {
        cmd_echo(argc);
        return;
    }

    /* panic test */
    if (strcmp(argv[0], "panic") == 0)
    {
        panic("manual panic");
    }
    if (strcmp(argv[0], "mkdir") == 0)
    {
        if (argc < 2)
        {
            println(RED, "usage: mkdir <name>");
            return;
        }
    
        fs_mkdir(argv[1]);
        return;
    }
    
    if (strcmp(argv[0], "cd") == 0)
    {
        if (argc < 2)
        {
            println(RED, "usage: cd <dir>");
            return;
        }
    
        if (fs_cd(argv[1]) != 0)
            println(RED, "dir not found");
    
        return;
    }

    println(RED, "unknown command");
}

/* =========================================================
   main shell loop
   ========================================================= */

void bssh_main(void)
{
    banner("Barnix Shell v0.2");

    while (1)
    {
        print(CYAN, "bssh> ");

        input(cmd, MAX_CMD, "");

        int argc = parse(cmd);

        exec_cmd(argc);
    }
}
