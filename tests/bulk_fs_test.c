/* Bulk mode keeps clean blocks across operations (Setup package installs).
 * Many small files, rewrites, removals and directory growth must stay
 * consistent with what is on disk, both during bulk mode and after it. */
#define FS_FILE_BACKED
#define main original_main
#include "fs_test.c"
#undef main
static void name_of(char *out, const char *dir, int i)
{
    strcpy(out, dir);
    int n = strlen(out);
    out[n++] = '/'; out[n++] = 'f';
    out[n++] = (char)('0' + i / 1000); out[n++] = (char)('0' + i / 100 % 10);
    out[n++] = (char)('0' + i / 10 % 10); out[n++] = (char)('0' + i % 10); out[n] = 0;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); disk_stream = fopen(argv[1], "r+b"); CHECK(disk_stream);
    mock_selection.sectors = 128U * 1024U * 2U;
    CHECK(fs_format_selected() == 0);
    CHECK(fs_init() == 0);
    static char data[70000], result[70000], path[64];
    for (unsigned int i = 0; i < sizeof(data); i++) data[i] = (char)(i * 7 % 253);
    fs_bulk_begin();
    CHECK(fs_mkdir("/usr") == 0); CHECK(fs_mkdir("/usr/include") == 0); CHECK(fs_mkdir("/usr/include/sys") == 0);
    for (int i = 0; i < 1200; i++) {
        name_of(path, i % 3 ? "/usr/include" : "/usr/include/sys", i);
        unsigned int size = (unsigned int)(i * 37 % 5000) + (i % 50 == 0 ? 60000U : 0U);
        CHECK(fs_write(path, "", 0) == 0);
        CHECK(fs_pwrite(path, 0, data + i % 97, size, 0) == (int)size);
        CHECK(fs_chmod(path, 0100755) == 0);
        if (i % 7 == 0) {   /* read back through the clean cache */
            CHECK(fs_read(path, 0, result, size) == (int)size);
            CHECK(!memcmp(result, data + i % 97, size));
        }
    }
    for (int i = 0; i < 1200; i += 5) { name_of(path, i % 3 ? "/usr/include" : "/usr/include/sys", i); CHECK(fs_rm(path) == 0); }
    for (int i = 0; i < 1200; i += 10) {   /* freed blocks are reallocated to new contents */
        name_of(path, "/usr/include/sys", i + 5000);
        CHECK(fs_write(path, data + 3, 3000) == 0);
    }
    CHECK(fs_bulk_end() == 0);
    CHECK(fs_init() == 0);
    for (int i = 0; i < 1200; i++) {
        name_of(path, i % 3 ? "/usr/include" : "/usr/include/sys", i);
        unsigned int size = (unsigned int)(i * 37 % 5000) + (i % 50 == 0 ? 60000U : 0U);
        if (i % 5 == 0) { CHECK(fs_size(path) < 0); continue; }
        CHECK(fs_read(path, 0, result, size) == (int)size);
        CHECK(!memcmp(result, data + i % 97, size));
    }
    for (int i = 0; i < 1200; i += 10) {
        name_of(path, "/usr/include/sys", i + 5000);
        CHECK(fs_read(path, 0, result, 3000) == 3000 && !memcmp(result, data + 3, 3000));
    }
    CHECK(fclose(disk_stream) == 0);
    puts("Bulk-mode clean cache: small files, removals, reuse and remount passed");
    return 0;
}
