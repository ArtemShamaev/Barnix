/* Freestanding integration checks. Identical executable and checks on Linux
 * and Barnix; no Barnix headers or function-pointer ABI. */
static int syscall3(int nr, unsigned int a, unsigned int b, unsigned int c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return result;
}
#define CALL(n,a,b,c) syscall3(n,(unsigned int)(a),(unsigned int)(b),(unsigned int)(c))
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
extern void _start(void);
int file_probe(unsigned int *initial)
{
    unsigned int *aux = initial + initial[0] + 2;
    while (*aux) aux++;
    aux++;
    unsigned int phdr = 0, phnum = 0, phent = 0, pagesize = 0, entry = 0;
    while (aux[0]) {
        if (aux[0] == 3) phdr = aux[1];
        if (aux[0] == 4) phent = aux[1];
        if (aux[0] == 5) phnum = aux[1];
        if (aux[0] == 6) pagesize = aux[1];
        if (aux[0] == 9) entry = aux[1];
        aux += 2;
    }
    CHECK(phdr && phnum && phent == 32 && pagesize == 4096);
    CHECK(entry == (unsigned int)_start);
    CHECK(*(unsigned int *)phdr == 1 || *(unsigned int *)phdr == 6);
    char buffer[8] = {0};
    char cwd[128];
    CHECK(CALL(183, cwd, sizeof(cwd), 0) > 1);
    char entries[1024];
    int directory = CALL(5, ".", 65536, 0);
    CHECK(directory >= 0);
    CHECK(CALL(141, directory, entries, 1) == -22);
    int bytes = CALL(141, directory, entries, sizeof(entries));
    CHECK(bytes >= 16);
    int found_dot = 0;
    for (int off = 0; off < bytes;) {
        unsigned char *entry = (unsigned char *)entries + off;
        unsigned int reclen = entry[8] | ((unsigned int)entry[9] << 8);
        CHECK(reclen >= 12 && reclen <= (unsigned int)(bytes - off));
        if (entry[10] == '.' && !entry[11]) {
            CHECK(entry[reclen - 1] == 4); found_dot = 1;
        }
        off += reclen;
    }
    CHECK(found_dot);
    CHECK(CALL(6, directory, 0, 0) == 0);
    const char *name = "linux-probe.dat";
    int fd = CALL(5, name, 2 | 64 | 512, 0600);
    CHECK(fd >= 0);
    CHECK(CALL(4, fd, "abc", 3) == 3);
    CHECK(CALL(19, fd, 0, 0) == 0);
    int copy = CALL(41, fd, 0, 0);
    CHECK(copy >= 0 && copy != fd);
    CHECK(CALL(3, copy, buffer, 1) == 1 && buffer[0] == 'a');
    CHECK(CALL(3, fd, buffer, 1) == 1 && buffer[0] == 'b');
    CHECK(CALL(4, copy, "Z", 1) == 1);
    CHECK(CALL(6, copy, 0, 0) == 0);
    CHECK(CALL(19, fd, 0, 0) == 0);
    CHECK(CALL(12, "/bin", 0, 0) == 0);
    CHECK(CALL(3, fd, buffer, 8) == 3);
    CHECK(CALL(12, cwd, 0, 0) == 0);
    CHECK(buffer[0] == 'a' && buffer[1] == 'b' && buffer[2] == 'Z');
    CHECK(CALL(19, fd, 6, 0) == 6);
    CHECK(CALL(4, fd, "!", 1) == 1);
    CHECK(CALL(19, fd, 3, 0) == 3);
    CHECK(CALL(3, fd, buffer, 8) == 4);
    CHECK(!buffer[0] && !buffer[1] && !buffer[2] && buffer[3] == '!');
    CHECK(CALL(19, fd, 0, 5) == -22);
    CHECK(CALL(19, fd, -1, 0) == -22);
    CHECK(CALL(19, fd, 0, 0) == 0);
    CHECK(CALL(3, fd, 0, 1) == -14);
    CHECK(CALL(6, fd, 0, 0) == 0);
    CHECK(CALL(6, fd, 0, 0) == -9);
    fd = CALL(5, name, 0, 0);
    CHECK(fd >= 0);
    CHECK(CALL(4, fd, "x", 1) == -9);
    CHECK(CALL(6, fd, 0, 0) == 0);
    fd = CALL(5, name, 1 | 1024, 0);
    CHECK(fd >= 0);
    CHECK(CALL(19, fd, 0, 0) == 0);
    CHECK(CALL(4, fd, "?", 1) == 1);
    CHECK(CALL(19, fd, 0, 1) == 8);
    CHECK(CALL(6, fd, 0, 0) == 0);
    CHECK(CALL(5, name, 1 | 64 | 128, 0600) == -17);
    int saved = CALL(41, 1, 0, 0);
    CHECK(saved >= 0);
    fd = CALL(5, name, 1 | 512, 0);
    CHECK(fd >= 0);
    CHECK(CALL(63, fd, 1, 0) == 1);
    CHECK(CALL(4, 1, "redirected", 10) == 10);
    CHECK(CALL(19, fd, 0, 1) == 10);
    CHECK(CALL(63, saved, 1, 0) == 1);
    CHECK(CALL(6, fd, 0, 0) == 0);
    CHECK(CALL(6, saved, 0, 0) == 0);
    CHECK(CALL(6, 0, 0, 0) == 0);
    fd = CALL(5, name, 0, 0);
    CHECK(fd == 0);
    CHECK(CALL(3, 0, buffer, 8) == 8 && buffer[0] == 'r');
    CHECK(CALL(6, 0, 0, 0) == 0);
    CHECK(CALL(12, "/bin", 0, 0) == 0);
    return 0;
}
