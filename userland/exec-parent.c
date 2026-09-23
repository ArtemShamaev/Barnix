#include "syscall.h"
#define CHECK(x) do { if (!(x)) return fail("exec parent failed: " #x); } while (0)
int main(int argc, const char *const *argv)
{
    CHECK(argc == 2);
    char cwd[256];
    CHECK(CALL(getcwd, cwd, sizeof(cwd), 0) > 0);
    const char *args[] = {"different-argv-zero", "child argument", "32", argv[1], cwd, 0};
    const char *env[] = {"BARNIX_EXEC=works", "EMPTY=", "UTF8=Привет", 0};
    int fd = CALL(open, "exec-state.dat", 2 | 64 | 512, 0600);
    CHECK(fd >= 0 && fd != 7);
    CHECK(CALL(dup2, fd, 7, 0) == 7);
    CHECK(CALL(dup2, fd, 8, 0) == 8);
    CHECK(CALL(fcntl, 8, 2, 1) == 0); /* FD_CLOEXEC */
    CHECK(CALL(dup2, fd, 11, 0) == 11);
    CHECK(CALL(close, fd, 0, 0) == 0);
    fd = CALL(open, "exec-state.dat", 2 | 02000000, 0); /* O_CLOEXEC */
    CHECK(fd >= 0);
    CHECK(CALL(dup2, fd, 9, 0) == 9); /* dup2 clears FD_CLOEXEC */
    CHECK(CALL(fcntl, fd, 1, 0) == 1);
    CHECK(CALL(fcntl, 9, 1, 0) == 0);
    CHECK(CALL(write, 7, "A", 1) == 1);
    CHECK(CALL(execve, 0, args, env) == -14);
    CHECK(CALL(execve, "/definitely-missing", args, env) == -2);
    CHECK(CALL(execve, "exec-state.dat", args, env) == -13);
    CHECK(CALL(execve, argv[1], 1, env) == -14);
    CHECK(CALL(execve, argv[1], args, 1) == -14);
    const char *bad[] = {(const char *)1, 0};
    CHECK(CALL(execve, argv[1], bad, env) == -14);
    CHECK(CALL(execve, argv[1], args, bad) == -14);
    CHECK(CALL(fcntl, 8, 1, 0) == 1); /* failed exec must not close it */
    CHECK(CALL(fcntl, fd, 1, 0) == 1);
    CHECK(CALL(lseek, 11, 0, 1) == 1);
    /* Linux replaces the process, so successful exec never reaches the return. */
    CALL(execve, argv[1], args, env);
    return fail("execve returned instead of replacing the image");
}
