#include "barnix.h"
#include "fs.h"
#include "disk.h"

#define MAX_CMD 128
#define MAX_ARGS (MAX_CMD / 2)

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
    println(WHITE, "write f txt - write text");
    println(WHITE, "rm <file>   - remove file");
    println(WHITE, "mkdir / cd  - create / enter directory");
    println(WHITE, "pwd         - current directory");
    println(WHITE, "rmdir <dir> - remove empty directory");
    println(WHITE, "cp src dst  - copy file (new name)");
    println(WHITE, "mv old new  - rename file or directory");
    println(WHITE, "append f txt- append text (no added newline)");
    println(WHITE, "stat <name> - file or directory details");
    println(WHITE, "df          - filesystem usage and limits");
    println(WHITE, "diskinfo    - disk type and capacity");
    println(WHITE, "sync        - save filesystem to disk");
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
static void report_result(int result)
{
    if (result != 0)
        println(RED, "operation failed (check name, type, space or disk)");
}

static void exec_cmd(int argc)
{
    if (argc == 0)
        return;

    if (strcmp(argv[0], "pwd") == 0) { fs_pwd(); return; }
    if (strcmp(argv[0], "df") == 0) { fs_df(); return; }
    if (strcmp(argv[0], "diskinfo") == 0) { disk_info(); return; }
    if (strcmp(argv[0], "sync") == 0)
    {
        if (fs_sync() == 0) println(GREEN, "filesystem saved");
        return;
    }
    if (strcmp(argv[0], "stat") == 0 || strcmp(argv[0], "rmdir") == 0)
    {
        if (argc != 2) { println(RED, "usage: stat <name> / rmdir <dir>"); return; }
        report_result(strcmp(argv[0], "stat") == 0 ? fs_stat(argv[1]) : fs_rmdir(argv[1]));
        return;
    }
    if (strcmp(argv[0], "cp") == 0 || strcmp(argv[0], "mv") == 0)
    {
        if (argc != 3) { println(RED, "usage: cp <src> <dst> / mv <old> <new>"); return; }
        report_result(strcmp(argv[0], "cp") == 0 ? fs_cp(argv[1], argv[2]) : fs_mv(argv[1], argv[2]));
        return;
    }

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

        report_result(fs_touch(argv[1]));
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

        report_result(fs_rm(argv[1]));
        return;
    }
    /* write */
    if (strcmp(argv[0], "write") == 0 || strcmp(argv[0], "append") == 0)
    {
        if (argc < 3)
        {
            println(RED, "usage: write/append <file> <text>");
            return;
        }

        /* собрать текст обратно */
        char buffer[MAX_CMD];
        buffer[0] = 0;

        for (int i = 2; i < argc; i++)
        {
            strcat(buffer, argv[i]);

            if (i + 1 < argc)
                strcat(buffer, " ");
        }

        report_result(strcmp(argv[0], "append") == 0 ?
            fs_append(argv[1], buffer, strlen(buffer)) :
            fs_write(argv[1], buffer, strlen(buffer)));
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
    
        report_result(fs_mkdir(argv[1]));
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
