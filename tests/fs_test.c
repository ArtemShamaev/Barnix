const char *tr(const char *s) { return s; }
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
static DiskSelection mock_selection = {DISK_RAM, 0, 4096, 0};
int disk_read_many(unsigned int lba, void *buffer, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
        if (disk_read(lba + i, (unsigned char *)buffer + i * 512)) return -1;
    return 0;
}
int disk_present(void) { return mock_selection.id != DISK_NONE; }
int disk_flush(void) { return fail_writes ? -1 : 0; }
DiskSelection disk_selection(void) { return mock_selection; }
void disk_restore(DiskSelection selection) { mock_selection = selection; }
void disk_deselect(void) { mock_selection.id = DISK_NONE; }
int disk_select(const char *name)
{
    if (strcmp(name, "ram0")) return -1;
    mock_selection = (DiskSelection){DISK_RAM, 0, 4096, 0}; return 0;
}
const char *disk_name(void) { return "ram0"; }
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
    CHECK(fs_size("large") == 256);
    CHECK(fs_rm("large") == 0);
    CHECK(fs_cp("link", "copy") == -1);
    CHECK(fs_write("link", "x", 1) == -1);
    CHECK(fs_rm("link") == -1);
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
    CHECK(fs_mkdir("язык") == 0);
    CHECK(fs_write("/язык/текст", "Привет", 12) == 0);
    CHECK(fs_cd("/язык") == 0);
    CHECK(fs_size("текст") == 12);
    CHECK(fs_write("/язык/текст", "Ёж", 4) == 0);
    CHECK(fs_size("текст") == 4); /* Absolute write preserves cwd. */
    CHECK(fs_write("/missing/текст", "x", 1) == -1);
    CHECK(fs_size("текст") == 4);
    CHECK(fs_rm("текст") == 0);
    CHECK(fs_cd("/") == 0);
    CHECK(fs_rmdir("язык") == 0);
    CHECK(fs_mkdir("dir") == 0);
    CHECK(fs_cp("dir", "copy") == -1);
    CHECK(fs_cd("dir") == 0);
    CHECK(fs_append("child", "x", 1) == 0);
    output[0] = 0; fs_pwd(); CHECK(strcmp(output, "/dir\n") == 0);
    char cwd_buffer[8];
    CHECK(fs_getcwd(cwd_buffer, sizeof(cwd_buffer)) == 5);
    CHECK(strcmp(cwd_buffer, "/dir") == 0);
    CHECK(fs_getcwd(cwd_buffer, 4) == -34);
    CHECK(fs_getcwd(NULL, 8) == -34);
    unsigned char entries[64]; unsigned int position = 0;
    CHECK(fs_getdents(".", entries, 1, &position) == -22 && position == 0);
    int entry_bytes = fs_getdents(".", entries, sizeof(entries), &position);
    CHECK(entry_bytes > 0 && position == 3);
    int children = 0;
    for (int offset = 0; offset < entry_bytes;) {
        unsigned char *entry = entries + offset;
        unsigned int reclen = get16(entry + 8);
        CHECK(reclen >= 12 && !(reclen & 3) && reclen <= (unsigned int)(entry_bytes - offset));
        if (!strcmp((char *)entry + 10, "child")) {
            CHECK(entry[reclen - 1] == 8); children++;
        } else {
            CHECK(!strcmp((char *)entry + 10, ".") || !strcmp((char *)entry + 10, ".."));
            CHECK(entry[reclen - 1] == 4);
        }
        offset += reclen;
    }
    CHECK(children == 1);
    CHECK(fs_getdents(".", entries, sizeof(entries), &position) == 0);
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
    CHECK(fs_write("a", "", FS_MAX_FILE_SIZE + 1) == -1);
    CHECK(fs_append("a", "", FS_MAX_FILE_SIZE + 1) == -1);
    expect_cat("a", "hello world\n");
    static char full[FS_MAX_FILE_SIZE], verify[FS_MAX_FILE_SIZE];
    for (unsigned int i = 0; i < sizeof(full); i++) full[i] = i % 251;
    CHECK(fs_size("binary") == 40000);
    CHECK(fs_read("binary", 12000, verify, 4096) == 4096);
    for (int i = 0; i < 4096; i++) CHECK((unsigned char)verify[i] == (12000 + i) % 251);
    CHECK(fs_read("binary", 40000, verify, 1) == 0);
    CHECK(fs_read("binary", 40001, verify, 0) == -1);
    CHECK(fs_read("binary", 0xffffffffU, verify, 1) == -1);
    CHECK(fs_read("sparse", 0, verify, 20000) == 20000);
    for (int i = 0; i < 19999; i++) CHECK(verify[i] == 0);
    CHECK(verify[19999] == 'z');
    CHECK(fs_append("sparse", "!", 1) == 0);
    CHECK(fs_write("full", full, sizeof(full)) == 0);
    CHECK(fs_size("full") == FS_MAX_FILE_SIZE);
    CHECK(fs_read("full", 0, verify, sizeof(verify)) == sizeof(verify));
    CHECK(memcmp(full, verify, sizeof(full)) == 0);
    CHECK(fs_cp("full", "fullcopy") == 0);
    CHECK(fs_init() == 0);
    CHECK(fs_read("fullcopy", 0, verify, sizeof(verify)) == sizeof(verify));
    CHECK(memcmp(full, verify, sizeof(full)) == 0);
    unsigned int free_before = get32(SB + 12);
    CHECK(fs_write("boundary", full, 12287) == 0);
    CHECK(fs_append("boundary", "xy", 2) == 0);
    CHECK(fs_read("boundary", 12287, verify, 2) == 2);
    CHECK(verify[0] == 'x' && verify[1] == 'y');
    CHECK(fs_write("boundary", full, 1025) == 0);
    CHECK(fs_size("boundary") == 1025);
    CHECK(fs_write("boundary", "", 0) == 0);
    CHECK(fs_rm("boundary") == 0);
    CHECK(get32(SB + 12) == free_before);
    /* Fill the remaining data blocks and ensure allocation failure rolls back. */
    int filled = 0;
    for (; filled < 10; filled++)
    {
        char name[] = {'f', (char)('0' + filled), 0};
        unsigned int free_blocks = get32(SB + 12), free_inodes = get32(SB + 16);
        if (fs_write(name, full, sizeof(full)) == -1)
        {
            CHECK(fs_size(name) == -1);
            CHECK(get32(SB + 12) == free_blocks && get32(SB + 16) == free_inodes);
            break;
        }
    }
    CHECK(filled > 0 && filled < 10);
    for (int i = 0; i < filled; i++)
    {
        char name[] = {'f', (char)('0' + i), 0}; CHECK(fs_rm(name) == 0);
    }
    CHECK(get32(SB + 12) == free_before);
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
