/* Host tests: real FS implementation with an in-memory sector device. */
#include "fs.c"

extern void abort(void);
extern int puts(const char *);
#define CHECK(expr) do { if (!(expr)) { puts("FAILED: " #expr); abort(); } } while (0)

static unsigned char test_disk[4096][DISK_SECTOR_SIZE];
static char output[4096];
static int fail_writes;
static int write_calls;
static int fail_after = -1;

int disk_read(unsigned int lba, void *buffer)
{
    if (lba >= 4096) return -1;
    memcpy(buffer, test_disk[lba], DISK_SECTOR_SIZE);
    return 0;
}
int disk_write(unsigned int lba, const void *buffer)
{
    write_calls++;
    if (lba >= 4096 || fail_writes || fail_after == 0) return -1;
    if (fail_after > 0) fail_after--;
    memcpy(test_disk[lba], buffer, DISK_SECTOR_SIZE);
    return 0;
}
int strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
void strcpy(char *a, const char *b) { while ((*a++ = *b++)) {} }
void strncpy(char *a, const char *b, int n)
{
    while (n-- > 0) { *a++ = *b; if (*b) b++; }
}
char *strchr(const char *s, int c)
{
    do { if (*s == c) return (char *)s; } while (*s++);
    return NULL;
}
void *memcpy(void *a, const void *b, int n)
{
    unsigned char *d = a; const unsigned char *s = b;
    while (n-- > 0) *d++ = *s++;
    return a;
}
void *memset(void *a, int c, int n)
{
    unsigned char *d = a;
    while (n-- > 0) *d++ = c;
    return a;
}
void print(int color, const char *s)
{
    (void)color;
    CHECK(strlen(output) + strlen(s) < (int)sizeof(output));
    strcpy(output + strlen(output), s);
}
void println(int color, const char *s) { print(color, s); print(color, "\n"); }
void print_int(int color, int n)
{
    char digit[2] = {(char)('0' + n % 10), 0};
    if (n >= 10) print_int(color, n / 10);
    print(color, digit);
}
static void expect_cat(const char *name, const char *expected)
{
    output[0] = 0; fs_cat(name); CHECK(strcmp(output, expected) == 0);
}
extern void *fopen(const char *, const char *);
extern size_t fread(void *, size_t, size_t, void *);
extern size_t fwrite(const void *, size_t, size_t, void *);
extern int fclose(void *);
int memcmp(const void *a, const void *b, int n)
{
    const unsigned char *x = a, *y = b;
    while (n-- > 0) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
int strncmp(const char *a, const char *b, int n)
{
    while (n-- > 0) { if (*a != *b) return *a - *b; if (!*a) return 0; a++; b++; }
    return 0;
}
void print_uint(int color, unsigned int n) { print_int(color, n); }
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    void *file = fopen(argv[1], "rb"); CHECK(file);
    CHECK(fread(test_disk, 1, sizeof(test_disk), file) == sizeof(test_disk));
    CHECK(fclose(file) == 0);
    CHECK(fs_init() == 0);
    expect_cat("host", "from-linux\n");
    int writes = write_calls;
    CHECK(fs_write("large", "x", 1) == -1);
    CHECK(fs_rm("large") == -1);
    CHECK(fs_cp("large", "copy") == -1);
    CHECK(fs_write("link", "x", 1) == -1);
    CHECK(fs_rm("link") == -1);
    CHECK(write_calls == writes);
    CHECK(fs_write("a", "hello", 5) == 0);
    CHECK(fs_append("a", " world", 6) == 0);
    CHECK(fs_cp("a", "b") == 0);
    CHECK(fs_cp("a", "b") == -1);
    CHECK(fs_mv("b", "a") == -1);
    CHECK(fs_mv("b", "c") == 0);
    CHECK(fs_stat("b") == -1);
    expect_cat("c", "hello world\n");
    CHECK(fs_init() == 0); /* Reload persisted metadata and contents. */
    expect_cat("c", "hello world\n");
    CHECK(fs_mkdir("dir") == 0);
    CHECK(fs_cp("dir", "copy") == -1);
    CHECK(fs_cd("dir") == 0);
    CHECK(fs_append("child", "x", 1) == 0);
    output[0] = 0; fs_pwd(); CHECK(strcmp(output, "/dir\n") == 0);
    CHECK(fs_cd("..") == 0);
    CHECK(fs_rmdir("dir") == -1);
    CHECK(fs_mv("dir", "renamed") == 0);
    CHECK(fs_cd("renamed") == 0);
    CHECK(fs_rm("child") == 0);
    CHECK(fs_cd("..") == 0);
    CHECK(fs_rmdir("renamed") == 0);
    CHECK(fs_rmdir("/") == -1);
    CHECK(fs_mv("/", "root") == -1);
    CHECK(fs_touch("bad/name") == -1);
    CHECK(fs_touch("123456789012345678901234") == -1);
    CHECK(fs_write("a", "", -1) == -1);
    CHECK(fs_write("a", "", 256) == -1);
    CHECK(fs_append("a", "", 256) == -1);
    expect_cat("a", "hello world\n");
    char full[255]; memset(full, 'x', sizeof(full));
    CHECK(fs_write("full", full, 255) == 0);
    CHECK(fs_append("full", "x", 1) == -1);
    CHECK(fs_append("full", "", 0) == 0);
    CHECK(fs_sync() == 0);
    int created = 0;
    for (int i = 0; i < 100; i++)
    {
        char name[24] = {'n', (char)('0' + i / 10), (char)('0' + i % 10), 0};
        if (i >= 2) { memset(name + 3, 'x', 20); name[23] = 0; }
        if (fs_touch(name) != 0) break;
        created++;
    }
    CHECK(created > 0 && created < 100);
    CHECK(fs_touch("overflow") == -1);
    CHECK(fs_cp("a", "overflow") == -1);
    CHECK(fs_rm("n00") == 0);
    CHECK(fs_cp("a", "reused") == 0);
    CHECK(fs_init() == 0);
    expect_cat("reused", "hello world\n");
    CHECK(fs_rm("full") == 0);
    CHECK(fs_mkdir("kept") == 0);
    CHECK(fs_cd("kept") == 0);
    CHECK(fs_write("nested", "ext2", 4) == -1); /* No free inode remains. */
    CHECK(fs_cd("/") == 0);
    CHECK(fs_rm("n01") == 0);
    CHECK(fs_cd("kept") == 0);
    CHECK(fs_write("nested", "ext2", 4) == 0);
    CHECK(fs_cd("/") == 0);
    CHECK(fs_rm("n02xxxxxxxxxxxxxxxxxxxx") == 0);
    file = fopen(argv[1], "wb"); CHECK(file);
    CHECK(fwrite(test_disk, 1, sizeof(test_disk), file) == sizeof(test_disk));
    CHECK(fclose(file) == 0);
    /* Failed disk writes stop subsequent mutations until remounted. */
    fail_writes = 1;
    CHECK(fs_write("a", "changed", 7) == -1);
    CHECK(fs_sync() == -1);
    fail_writes = 0;
    CHECK(fs_init() == 0);
    expect_cat("a", "hello world\n");
    /* A failure after the dirty marker prevents mounting until repaired. */
    fail_after = 1;
    CHECK(fs_write("a", "changed", 7) == -1);
    CHECK(get16(test_disk[2] + 58) == 2);
    CHECK(fs_init() == -1);
    fail_after = -1;
    put16(test_disk[2] + 58, 1); /* The data write never happened in this test. */
    CHECK(fs_init() == 0);
    /* Unsupported and corrupt images never trigger formatting/writes. */
    writes = write_calls;
    unsigned int revision = get32(test_disk[2] + 76);
    put32(test_disk[2] + 76, 1);
    put32(test_disk[2] + 96, 0x40); /* extents, not supported */
    CHECK(fs_init() == -1);
    put32(test_disk[2] + 76, revision);
    put32(test_disk[2] + 96, filetype ? 2 : 0);
    CHECK(fs_init() == 0);
    unsigned int root_sector = get32(inode(ROOT_INO) + 40) * 2;
    put16(test_disk[root_sector] + 4, 0); /* Invalid directory record length. */
    CHECK(fs_init() == -1);
    CHECK(fs_touch("refused") == -1);
    test_disk[2][56] = 0;
    CHECK(fs_init() == -1);
    CHECK(test_disk[2][56] == 0);
    CHECK(write_calls == writes);
    puts("Ext2 FS tests passed");
    return 0;
}
