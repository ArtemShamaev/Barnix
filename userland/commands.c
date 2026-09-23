#include "syscall.h"

static int copy(int input, int output)
{
    char buffer[4096];
    for (;;) {
        int got = CALL(read, input, buffer, sizeof(buffer));
        if (got == -4) continue;
        if (got < 0) return -1;
        if (!got) return 0;
        if (write_all(output, buffer, got)) return -1;
    }
}
static int list(const char *path)
{
    int fd = CALL(open, path, 65536, 0); /* O_RDONLY | O_DIRECTORY */
    if (fd < 0) return fail("ls: cannot open directory");
    char entries[1024];
    int result = 0, count;
    while ((count = CALL(getdents, fd, entries, sizeof(entries))) > 0) {
        for (int offset = 0; offset < count;) {
            unsigned char *entry = (unsigned char *)entries + offset;
            if (count - offset < 12) { result = 1; break; }
            unsigned int size = entry[8] | ((unsigned int)entry[9] << 8);
            if (size < 12 || size > (unsigned int)(count - offset)) { result = 1; break; }
            char *name = (char *)entry + 10;
            unsigned int n = 0;
            while (n < size - 11 && name[n]) n++;
            if (n == size - 11) { result = 1; break; }
            if (!equal(name, ".") && !equal(name, "..")) {
                if (write_all(1, name, n) || (entry[size - 1] == 4 && text(1, "/")) || text(1, "\n"))
                    result = 1;
            }
            offset += size;
        }
        if (result) break;
    }
    if (count < 0) result = 1;
    if (CALL(close, fd, 0, 0)) result = 1;
    return result;
}
int main(int argc, const char *const *argv)
{
    if (!argc) return 1;
    /* Each /bin executable selects its command at build time, not via argv[0]. */
    const char *command = COMMAND;
    if (equal(command, "true")) return 0;
    if (equal(command, "false")) return 1;
    if (equal(command, "echo")) {
        for (int i = 1; i < argc; i++)
            if ((i > 1 && text(1, " ")) || text(1, argv[i])) return 1;
        return text(1, "\n") ? 1 : 0;
    }
    if (equal(command, "pwd")) {
        char cwd[4096];
        if (argc != 1) return fail("usage: pwd");
        if (CALL(getcwd, cwd, sizeof(cwd), 0) < 0) return fail("pwd: operation failed");
        return text(1, cwd) || text(1, "\n") ? 1 : 0;
    }
    if (equal(command, "ls")) {
        if (argc > 2) return fail("usage: ls [directory]");
        return list(argc == 2 ? argv[1] : ".");
    }
    if (equal(command, "cat")) {
        if (argc == 1) return copy(0, 1) ? 1 : 0;
        for (int i = 1; i < argc; i++) {
            int fd = CALL(open, argv[i], 0, 0);
            if (fd < 0) return fail("cat: cannot open file");
            int result = copy(fd, 1);
            if (CALL(close, fd, 0, 0) || result) return fail("cat: operation failed");
        }
        return 0;
    }
    if (equal(command, "cp")) {
        if (argc != 3) return fail("usage: cp <source> <destination>");
        int input = CALL(open, argv[1], 0, 0);
        if (input < 0) return fail("cp: cannot open source");
        int output = CALL(open, argv[2], 1 | 64 | 128, 0644);
        if (output < 0) { CALL(close, input, 0, 0); return fail("cp: cannot create destination"); }
        int result = copy(input, output);
        if (CALL(close, input, 0, 0)) result = -1;
        if (CALL(close, output, 0, 0)) result = -1;
        return result ? fail("cp: operation failed") : 0;
    }
    int touch = equal(command, "touch");
    if ((touch && argc != 2) || (!touch && argc < 3)) return fail("missing file or text argument");
    unsigned int flags = 1 | 64 | (touch ? 0 : equal(command, "append") ? 1024 : 512);
    int fd = CALL(open, argv[1], flags, 0644);
    if (fd < 0) return fail("cannot open file");
    int result = 0;
    for (int i = 2; i < argc; i++)
        if ((i > 2 && text(fd, " ")) || text(fd, argv[i])) { result = 1; break; }
    if (CALL(close, fd, 0, 0)) result = 1;
    return result;
}
