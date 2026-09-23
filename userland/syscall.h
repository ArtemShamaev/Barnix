#ifndef BARNIX_USER_SYSCALL_H
#define BARNIX_USER_SYSCALL_H
#include "../include/uapi/unistd.h"
static inline int syscall3(unsigned int nr, unsigned int a, unsigned int b, unsigned int c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return result;
}
#define CALL(n, a, b, c) syscall3(__NR_##n, (unsigned int)(a), (unsigned int)(b), (unsigned int)(c))
static inline unsigned int length(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static inline int equal(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline int write_all(int fd, const char *s, unsigned int n)
{
    while (n) {
        int done = CALL(write, fd, s, n);
        if (done == -4) continue;
        if (done <= 0) return -1;
        s += done; n -= done;
    }
    return 0;
}
static inline int text(int fd, const char *s) { return write_all(fd, s, length(s)); }
static inline int fail(const char *message) { text(2, message); text(2, "\n"); return 1; }
#endif
