/* Nova launcher for Barnix BJVM. Compilation remains a host JDK 21 tool. */
#include "command.h"

#define EXAMPLES "/usr/share/nova/examples/"
static char output[32768];
static int same(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void copy(char *out, const char *in, unsigned int size)
{ unsigned int i = 0; while (i + 1 < size && in[i]) { out[i] = in[i]; i++; } out[i] = 0; }
static void append(char *out, const char *in, unsigned int size)
{ unsigned int n = strlen(out); if (n < size) copy(out + n, in, size - n); }
static int java_jar(const char *jar, int argc, const char *const *args)
{
    if (!barnix->run) return fail("nova: this kernel cannot launch BJVM from a command");
    unsigned int bytes = (APP_STACK_TOP - APP_LOAD_MIN) + sizeof(output);
    char *swap = barnix->memory(&bytes);
    if (!swap || bytes < APP_STACK_TOP - APP_LOAD_MIN) return fail("nova: not enough memory to start BJVM");
    const char *argv[32] = {"java", "-jar", jar};
    if (argc > 29) return fail("nova: too many arguments");
    for (int i = 0; i < argc; i++) argv[3 + i] = args[i];
    int status = 0;
    int n = barnix->run("/usr/bin/java", 3 + argc, argv, output, sizeof(output), swap, &status);
    if (n < 0) return fail("nova: BJVM or JAR could not be started");
    if (n) barnix->print(status ? 4 : 15, output);
    return status;
}
int main(int argc, const char *const *argv)
{
    if (argc == 1 || same(argv[1], "--help") || same(argv[1], "help")) {
        barnix->puts("Nova on Barnix: Java 8 bytecode runs on BJVM.");
        barnix->puts("  nova hello|shapes|sort|strings   run an installed example");
        barnix->puts("  nova run FILE.jar [args...]    run a compiled Nova JAR");
        barnix->puts("  nova build FILE.nova          compile on host with JDK 21");
        barnix->puts("  examples: /usr/share/nova/examples/");
        return 0;
    }
    if (same(argv[1], "build") || same(argv[1], "check"))
        return fail("nova: compilation needs JDK 21 on the host; use tools/nova/nova build FILE.nova");
    if (same(argv[1], "run")) {
        if (argc < 3) return fail("usage: nova run FILE.jar [args...]");
        return java_jar(argv[2], argc - 3, argv + 3);
    }
    if (argc > 2 || !(same(argv[1], "hello") || same(argv[1], "shapes") ||
                       same(argv[1], "sort") || same(argv[1], "strings")))
        return fail("nova: unknown example; run nova for usage");
    char jar[80]; copy(jar, EXAMPLES, sizeof(jar)); append(jar, argv[1], sizeof(jar)); append(jar, ".jar", sizeof(jar));
    return java_jar(jar, 0, 0);
}
