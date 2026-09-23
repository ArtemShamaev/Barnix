#include "syscall.h"

#define CHECK(x) do { if (!(x)) return fail("pipe probe failed: " #x); } while (0)
enum { NONBLOCK = 2048, CLOEXEC = 02000000, PAYLOAD = 131073 };
typedef struct { void *base; unsigned int length; } Vector;
static char payload[PAYLOAD], received[PAYLOAD];

static int reap(int child, int expected)
{
    int status = -1;
    CHECK(CALL(waitpid, child, &status, 0) == child && status == expected);
    return 0;
}
static int close_pair(int *p)
{
    CHECK(CALL(close, p[0], 0, 0) == 0);
    CHECK(CALL(close, p[1], 0, 0) == 0);
    return 0;
}
static int basic(void)
{
    int p[2] = {-1, -1};
    CHECK(CALL(pipe2, p, 1, 0) == -22 && p[0] == -1 && p[1] == -1);
    CHECK(CALL(pipe, 0, 0, 0) == -14);
    CHECK(CALL(pipe2, p, NONBLOCK | CLOEXEC, 0) == 0);
    CHECK(p[0] == 3 && p[1] == 4);
    CHECK(CALL(fcntl, p[0], 1, 0) == 1 && CALL(fcntl, p[1], 1, 0) == 1);
    CHECK(CALL(fcntl, p[0], 3, 0) == NONBLOCK);
    CHECK(CALL(fcntl, p[1], 3, 0) == (NONBLOCK | 1));
    CHECK(CALL(lseek, p[0], 0, 0) == -29);
    CHECK(CALL(write, p[0], payload, 1) == -9);
    CHECK(CALL(read, p[1], payload, 1) == -9);
    CHECK(CALL(read, p[0], 0, 0) == 0 && CALL(write, p[1], 0, 0) == 0);
    CHECK(CALL(read, p[0], payload, 1) == -11);
    CHECK(CALL(write, p[1], 0, 1) == -14);
    Vector invalid[] = {{payload, 1}, {0, 1}};
    CHECK(CALL(writev, p[1], invalid, 2) == -14);
    CHECK(CALL(readv, p[0], 0, 1) == -14);
    CHECK(CALL(writev, p[1], invalid, 1025) == -22);
    CHECK(CALL(writev, p[1], 0, 0) == 0);
    int duplicate = CALL(dup, p[1], 0, 0);
    CHECK(duplicate == 5 && CALL(fcntl, duplicate, 1, 0) == 0);
    CHECK(CALL(fcntl, duplicate, 4, 1) == 0);
    CHECK(CALL(fcntl, p[1], 3, 0) == 1);
    CHECK(CALL(fcntl, p[0], 3, 0) == NONBLOCK);
    CHECK(CALL(fcntl, duplicate, 4, 1 | NONBLOCK) == 0);
    int capacity = CALL(fcntl, p[0], 1032, 0);
    CHECK(capacity >= 4096 && capacity < PAYLOAD);
    for (int i = 0; i < capacity; i++) payload[i] = 'q';
    CHECK(CALL(write, p[1], payload, capacity) == capacity);
    int available = -1;
    CHECK(CALL(ioctl, p[0], 0x541b, &available) == 0 && available == capacity);
    CHECK(CALL(write, p[1], "x", 1) == -11);
    CHECK(CALL(read, p[0], received, 4) == 4);
    Vector small[] = {{"AB", 2}, {0, 0}, {"CDE", 3}};
    CHECK(CALL(writev, p[1], small, 3) == -11); /* Atomic across all vectors. */
    CHECK(CALL(ioctl, p[0], 0x541b, &available) == 0 && available == capacity - 4);
    /* Linux accounts buffer space in pages: freeing four bytes need not
     * make a write possible until the rest of that page is consumed. */
    CHECK(CALL(read, p[0], received, 4092) == 4092);
    small[2].length = 2;
    CHECK(CALL(writev, p[1], small, 3) == 4);
    CHECK(CALL(close, p[1], 0, 0) == 0);
    int remaining = capacity - 4096 + 4;
    Vector output[] = {{received, 3}, {0, 0}, {received + 3, (unsigned int)remaining - 3}};
    CHECK(CALL(readv, p[0], output, 3) == remaining);
    for (int i = 0; i < remaining - 4; i++) CHECK(received[i] == 'q');
    CHECK(received[remaining - 4] == 'A' && received[remaining - 1] == 'D');
    CHECK(CALL(read, p[0], received, 1) == -11); /* dup still owns the writer. */
    int partial = CALL(write, duplicate, payload, PAYLOAD);
    CHECK(partial > 0 && partial < PAYLOAD);
    CHECK(CALL(read, p[0], received, PAYLOAD) == partial);
    CHECK(CALL(close, duplicate, 0, 0) == 0);
    CHECK(CALL(read, p[0], received, 1) == 0);
    CHECK(CALL(close, p[0], 0, 0) == 0);
    /* Failure with just one free descriptor must not consume it. Both test
     * runners set the descriptor limit to Barnix's current limit of 16. */
    int held[12];
    for (int i = 0; i < 12; i++) { held[i] = CALL(dup, 0, 0, 0); CHECK(held[i] == i + 3); }
    p[0] = p[1] = -1;
    CHECK(CALL(pipe, p, 0, 0) == -24 && p[0] == -1 && p[1] == -1);
    CHECK(CALL(dup, 0, 0, 0) == 15 && CALL(close, 15, 0, 0) == 0);
    for (int i = 0; i < 12; i++) CHECK(CALL(close, held[i], 0, 0) == 0);
    for (int i = 0; i < 64; i++) { CHECK(CALL(pipe, p, 0, 0) == 0); CHECK(close_pair(p) == 0); }
    return 0;
}
static int stream(int vectored)
{
    int p[2];
    CHECK(CALL(pipe, p, 0, 0) == 0);
    for (int i = 0; i < PAYLOAD; i++) payload[i] = (char)(i * 37 + 11);
    int child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) {
        CHECK(CALL(close, p[0], 0, 0) == 0);
        Vector input[] = {{payload, 4093}, {0, 0}, {payload + 4093, PAYLOAD - 4093}};
        int result = vectored ? CALL(writev, p[1], input, 3) : CALL(write, p[1], payload, PAYLOAD);
        CHECK(result == PAYLOAD); /* A blocking write must finish across many switches. */
        CALL(exit, 0, 0, 0); /* Implicit close must wake the reader with EOF. */
        return 1;
    }
    CHECK(CALL(close, p[1], 0, 0) == 0);
    unsigned int total = 0;
    while (total < PAYLOAD) {
        unsigned int wanted = PAYLOAD - total;
        if (wanted > 3001) wanted = 3001;
        Vector output[] = {{received + total, 1}, {received + total + 1, wanted - 1}};
        int got = vectored ? CALL(readv, p[0], output, 2) : CALL(read, p[0], received + total, wanted);
        CHECK(got > 0 && (unsigned int)got <= wanted);
        total += got;
    }
    CHECK(CALL(read, p[0], received, 1) == 0);
    for (int i = 0; i < PAYLOAD; i++) CHECK(received[i] == payload[i]);
    CHECK(CALL(close, p[0], 0, 0) == 0);
    CHECK(reap(child, 0) == 0);
    return 0;
}
static int sigpipe(void)
{
    /* With a full pipe, the writer sleeps until the last reader closes. */
    int p[2];
    CHECK(CALL(pipe2, p, NONBLOCK, 0) == 0);
    int capacity = CALL(fcntl, p[0], 1032, 0);
    CHECK(CALL(write, p[1], payload, capacity) == capacity);
    CHECK(CALL(fcntl, p[1], 4, 1) == 0);
    int child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) {
        CHECK(CALL(close, p[0], 0, 0) == 0);
        CALL(write, p[1], "x", 1);
        CALL(exit, 99, 0, 0);
    }
    CHECK(close_pair(p) == 0);
    CHECK(reap(child, 13) == 0);
    return 0;
}
static int atomic_writers(void)
{
    int p[2], children[2];
    CHECK(CALL(pipe, p, 0, 0) == 0);
    for (int writer = 0; writer < 2; writer++) {
        children[writer] = CALL(fork, 0, 0, 0);
        CHECK(children[writer] >= 0);
        if (!children[writer]) {
            CHECK(CALL(close, p[0], 0, 0) == 0);
            for (int i = 0; i < 128; i++) payload[i] = 'a' + writer;
            Vector record[] = {{payload, 53}, {payload + 53, 75}};
            for (int i = 0; i < 64; i++) CHECK(CALL(writev, p[1], record, 2) == 128);
            CALL(exit, 0, 0, 0);
        }
    }
    CHECK(CALL(close, p[1], 0, 0) == 0);
    unsigned int total = 0;
    for (;;) {
        int got = CALL(read, p[0], received + total, 257);
        CHECK(got >= 0);
        if (!got) break;
        total += got;
        CHECK(total <= 128 * 128);
    }
    CHECK(total == 128 * 128);
    int counts[2] = {0, 0};
    for (unsigned int i = 0; i < total; i += 128) {
        int writer = received[i] - 'a';
        CHECK(writer == 0 || writer == 1);
        counts[writer]++;
        for (int j = 1; j < 128; j++) CHECK(received[i + j] == received[i]);
    }
    CHECK(counts[0] == 64 && counts[1] == 64);
    CHECK(CALL(close, p[0], 0, 0) == 0);
    CHECK(reap(children[0], 0) == 0 && reap(children[1], 0) == 0);
    return 0;
}
static int cloexec(const char *self)
{
    int p[2];
    CHECK(CALL(pipe2, p, CLOEXEC, 0) == 0 && p[0] == 3 && p[1] == 4);
    int child = CALL(fork, 0, 0, 0);
    CHECK(child >= 0);
    if (!child) {
        CHECK(CALL(dup2, p[1], 1, 0) == 1);
        const char *args[] = {self, "cloexec-check", 0};
        CALL(execve, self, args, 0);
        return fail("pipe self exec failed");
    }
    CHECK(CALL(close, p[1], 0, 0) == 0);
    CHECK(CALL(read, p[0], received, 1) == 1 && received[0] == 'K');
    CHECK(CALL(read, p[0], received, 1) == 0);
    CHECK(CALL(close, p[0], 0, 0) == 0);
    CHECK(reap(child, 0) == 0);
    return 0;
}
static int exec_pipeline(const char *echo, const char *cat)
{
    int p[2];
    CHECK(CALL(pipe2, p, CLOEXEC, 0) == 0);
    int writer = CALL(fork, 0, 0, 0);
    CHECK(writer >= 0);
    if (!writer) {
        CHECK(CALL(dup2, p[1], 1, 0) == 1);
        const char *args[] = {"echo", "pipe", "exec", "passed", 0};
        CALL(execve, echo, args, 0);
        return fail("pipe echo exec failed");
    }
    CHECK(CALL(close, p[1], 0, 0) == 0);
    int reader = CALL(fork, 0, 0, 0);
    CHECK(reader >= 0);
    if (!reader) {
        CHECK(CALL(dup2, p[0], 0, 0) == 0);
        int output = CALL(open, "pipe-state.dat", 1 | 64 | 512, 0600);
        CHECK(output >= 0 && CALL(dup2, output, 1, 0) == 1);
        CHECK(CALL(close, output, 0, 0) == 0);
        const char *args[] = {"cat", 0};
        CALL(execve, cat, args, 0);
        return fail("pipe cat exec failed");
    }
    CHECK(CALL(close, p[0], 0, 0) == 0);
    CHECK(reap(writer, 0) == 0 && reap(reader, 0) == 0);
    int fd = CALL(open, "pipe-state.dat", 0, 0);
    CHECK(fd >= 0 && CALL(read, fd, received, 32) == 17);
    received[17] = 0;
    CHECK(equal(received, "pipe exec passed\n"));
    CHECK(CALL(close, fd, 0, 0) == 0);
    return 0;
}
int main(int argc, const char *const *argv)
{
    if (argc == 2 && equal(argv[1], "cloexec-check")) {
        CHECK(CALL(fcntl, 3, 1, 0) == -9 && CALL(fcntl, 4, 1, 0) == -9);
        CHECK(CALL(fcntl, 1, 1, 0) == 0 && CALL(write, 1, "K", 1) == 1);
        return 0;
    }
    /* Barnix-only deadlock recovery is exercised as a separate launch. */
    if (argc == 2 && equal(argv[1], "deadlock")) {
        int p[2];
        CHECK(CALL(pipe, p, 0, 0) == 0);
        CALL(read, p[0], received, 1);
        return fail("deadlocked read returned to userspace");
    }
    CHECK(argc == 3);
    CHECK(basic() == 0);
    CHECK(stream(0) == 0 && stream(1) == 0);
    CHECK(sigpipe() == 0);
    CHECK(atomic_writers() == 0);
    CHECK(cloexec(argv[0]) == 0);
    CHECK(exec_pipeline(argv[1], argv[2]) == 0);
    CHECK(text(1, "pipe blocking/nonblocking/fork/exec passed\n") == 0);
    return 44;
}
