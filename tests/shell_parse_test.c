#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shell_parse.h"

int main(void)
{
    ShellPlan p;
    const char *line = "echo 'a | b' \"c > d\" e\\ f ''|cat<in>>out";
    assert(!shell_parse(line, &p) && p.count == 2 && p.compound);
    assert(p.commands[0].argc == 5 && !p.commands[0].argv[5]);
    assert(!strcmp(p.commands[0].argv[1], "a | b"));
    assert(!strcmp(p.commands[0].argv[2], "c > d"));
    assert(!strcmp(p.commands[0].argv[3], "e f"));
    assert(!strcmp(p.commands[0].argv[4], ""));
    assert(p.commands[1].argc == 1 && p.commands[1].redirect_count == 2);
    assert(p.commands[1].redirects[0].kind == SHELL_INPUT);
    assert(!strcmp(p.commands[1].redirects[0].path, "in"));
    assert(p.commands[1].redirects[1].kind == SHELL_APPEND);
    assert(!strcmp(p.commands[1].redirects[1].path, "out"));
    assert(!shell_parse("echo pre\" mid \"'post' \\| \\> \\<", &p));
    assert(!p.compound && p.commands[0].argc == 5);
    assert(!strcmp(p.commands[0].argv[1], "pre mid post"));
    assert(!strcmp(p.commands[0].argv[2], "|"));
    assert(!shell_parse("echo \"a\\q\" \"a\\\"b\" '\\x'", &p));
    assert(!strcmp(p.commands[0].argv[1], "a\\q"));
    assert(!strcmp(p.commands[0].argv[2], "a\"b"));
    assert(!strcmp(p.commands[0].argv[3], "\\x"));
    assert(!shell_parse(">one echo 2 >two >>three", &p));
    assert(p.commands[0].argc == 2 && p.commands[0].redirect_count == 3);
    assert(!strcmp(p.commands[0].redirects[0].path, "one"));
    assert(p.commands[0].redirects[1].kind == SHELL_OUTPUT);
    assert(p.commands[0].redirects[2].kind == SHELL_APPEND);
    assert(!shell_parse("echo '2'>file", &p));
    const char *invalid[] = {"|cat", "echo x|", "echo||cat", "echo|cat|cat",
        "echo >", "echo >>>file", "echo <|cat", "echo <<in", "echo 2>file",
        "echo >&1", "echo && cat", "echo &", "echo;cat", "(echo)",
        "echo 'bad", "echo \"bad", "echo bad\\", ">file", "''", "echo\ncat",
        "echo>a>b>c>d>e>f>g>h>i", 0};
    for (int i = 0; invalid[i]; i++) assert(shell_parse(invalid[i], &p));
    assert(!shell_parse(" \t ", &p) && !p.count);
    assert(!shell_parse("echo\tx", &p) && p.commands[0].argc == 2);
    char boundary[SHELL_LINE_MAX + 1];
    for (int i = 0; i < SHELL_LINE_MAX - 1; i++) boundary[i] = i & 1 ? ' ' : 'x';
    boundary[SHELL_LINE_MAX - 1] = 0;
    assert(!shell_parse(boundary, &p) && p.commands[0].argc == SHELL_ARGS_MAX);
    boundary[SHELL_LINE_MAX - 1] = 'x'; boundary[SHELL_LINE_MAX] = 0;
    assert(shell_parse(boundary, &p));
    puts("Shell parser: quotes, escapes, pipes, redirection order, errors and bounds passed");
    return 0;
}
