#include "syscall.h"
#define CHECK(x) do { if (!(x)) return fail("fork probe failed: " #x); } while (0)
static volatile unsigned int cookie = 12345;
int main(int argc, const char *const *argv)
{
    CHECK(argc == 2 || argc == 3);
    int parent = CALL(getpid, 0, 0, 0);
    char cwd[256], after[256];
    CHECK(CALL(getcwd, cwd, sizeof(cwd), 0) > 0);
    int fd = CALL(open, "fork-state.dat", 2 | 64 | 512, 0600);
    CHECK(fd >= 0 && CALL(write, fd, "A", 1) == 1);
    unsigned int original_brk = CALL(brk, 0, 0, 0);
    int child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) {
        CHECK(CALL(getpid, 0, 0, 0) != parent && CALL(getppid, 0, 0, 0) == parent);
        cookie = 999;
        CHECK((unsigned int)CALL(brk, original_brk + 4096, 0, 0) == original_brk + 4096);
        *(volatile char *)original_brk = 42;
        CHECK(CALL(sched_yield, 0, 0, 0) == 0);
        CHECK(CALL(write, fd, "B", 1) == 1);
        CHECK(CALL(close, fd, 0, 0) == 0);
        CHECK(CALL(chdir, "/bin", 0, 0) == 0);
        const char *args[] = {"exec-from-child", 0};
        const char *env[] = {"FORK=child", 0};
        CALL(execve, argv[1], args, env);
        return fail("child exec failed");
    }
    int status = -1;
    int waited = CALL(waitpid, child, &status, 1);
    CHECK(waited == 0 || waited == child);
    if (!waited) CHECK(CALL(waitpid, child, &status, 0) == child);
    CHECK(status == 0 && cookie == 12345);
    CHECK((unsigned int)CALL(brk, 0, 0, 0) == original_brk);
    CHECK(CALL(waitpid, child, &status, 0) == -10);
    CHECK(CALL(getcwd, after, sizeof(after), 0) > 0 && equal(cwd, after));
    CHECK(CALL(lseek, fd, 0, 1) == 2);
    CHECK(CALL(lseek, fd, 0, 0) == 0);
    char bytes[2];
    CHECK(CALL(read, fd, bytes, 2) == 2 && bytes[0] == 'A' && bytes[1] == 'B');
    CHECK(CALL(close, fd, 0, 0) == 0);
    child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) { __asm__ volatile("movl $1, 0"); return 99; }
    CHECK(CALL(waitpid, child, &status, 0) == child && (status & 127) == 11);
    for (int i = 0; i < 16; i++) {
        child = CALL(fork, 0, 0, 0);
        CHECK(child >= 0);
        if (!child) return i;
        CHECK(CALL(waitpid, -1, &status, 0) == child && status == (i << 8));
    }
    child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) {
        int grandchild = CALL(fork, 0, 0, 0);
        CHECK(grandchild >= 0);
        if (!grandchild) {
            if (argc == 3) CHECK(CALL(fork, 0, 0, 0) == -11);
            return 23;
        }
        CHECK(CALL(waitpid, grandchild, &status, 0) == grandchild && status == (23 << 8));
        return 17;
    }
    CHECK(CALL(waitpid, child, &status, 0) == child && status == (17 << 8));
    CHECK(CALL(waitpid, -1, &status, 1) == -10);
    CHECK(text(1, "fork/wait/exec isolation passed\n") == 0);
    return 43;
}
