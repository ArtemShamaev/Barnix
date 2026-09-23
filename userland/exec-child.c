#include "syscall.h"
#define CHECK(x) do { if (!(x)) return fail("exec child failed: " #x); } while (0)
static volatile unsigned int bss;
static volatile unsigned int data = 1234;
int main(int argc, const char *const *argv, const char *const *envp)
{
    CHECK(argc == 5 && argv[5] == 0);
    CHECK(equal(argv[0], "different-argv-zero") && equal(argv[1], "child argument"));
    CHECK(envp && envp[0] && envp[1] && envp[2] && !envp[3]);
    CHECK(equal(envp[0], "BARNIX_EXEC=works") && equal(envp[1], "EMPTY="));
    CHECK(equal(envp[2], "UTF8=Привет"));
    CHECK(bss == 0 && data == 1234);
    bss = 5678; data = 0;
    char cwd[256];
    CHECK(CALL(getcwd, cwd, sizeof(cwd), 0) > 0 && equal(cwd, argv[4]));
    CHECK(CALL(fcntl, 8, 1, 0) == -9);
    CHECK(CALL(fcntl, 3, 1, 0) == -9);
    CHECK(CALL(fcntl, 7, 1, 0) == 0 && CALL(fcntl, 9, 1, 0) == 0);
    unsigned int count = 0;
    for (const char *p = argv[2]; *p; p++) count = count * 10 + (*p - '0');
    CHECK(count <= 32);
    CHECK(CALL(lseek, 11, 0, 1) == (int)(33 - count));
    CHECK(CALL(write, 7, "B", 1) == 1);
    CHECK(CALL(lseek, 11, 0, 1) == (int)(34 - count));
    if (count) {
        char number[3];
        count--;
        if (count >= 10) { number[0] = '0' + count / 10; number[1] = '0' + count % 10; number[2] = 0; }
        else { number[0] = '0' + count; number[1] = 0; }
        const char *args[] = {argv[0], argv[1], number, argv[3], argv[4], 0};
        CALL(execve, argv[3], args, envp);
        return fail("repeated exec returned");
    }
    CHECK(CALL(close, 7, 0, 0) == 0);
    CHECK(CALL(close, 9, 0, 0) == 0);
    CHECK(CALL(close, 11, 0, 0) == 0);
    CHECK(text(1, "execve argv/env/fds/replacement passed\n") == 0);
    return 42;
}
