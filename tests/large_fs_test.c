#define FS_FILE_BACKED
#define main original_main
#include "fs_test.c"
#undef main
int main(int argc, char **argv)
{
    CHECK(argc == 2); disk_stream = fopen(argv[1], "r+b"); CHECK(disk_stream);
    mock_selection.sectors = 8U * 1024U * 1024U * 2U;
    CHECK(fs_format_selected() == 0);
    CHECK(blocks == FS_MAX_BLOCKS && groups == 640);
    CHECK(fs_init() == 0);
    unsigned int initial_free = get32(SB + 12);
    static char contents[FS_MAX_FILE_SIZE], result[FS_MAX_FILE_SIZE];
    for (unsigned int i = 0; i < sizeof(contents); i++) contents[i] = i % 251;
    /* Force a real allocation beyond 4 GiB, without filling the entire disk. */
    allocation_hint[0] = groups - 1;
    CHECK(fs_write("far", contents, sizeof(contents)) == 0);
    int n = lookup_path("far"); CHECK(n > 0);
    CHECK(file_block(inode(n), 0) > 4U * 1024U * 1024U);
    CHECK(fs_init() == 0);
    CHECK(fs_read("far", 0, result, sizeof(result)) == sizeof(result));
    CHECK(!memcmp(contents, result, sizeof(result)));
    CHECK(fs_append("far", "x", 1) == -1);
    CHECK(fs_mkdir("many") == 0); CHECK(fs_cd("many") == 0);
    for (int i = 0; i < 160; i++) {
        char name[] = {'f', (char)('0' + i / 100), (char)('0' + i / 10 % 10), (char)('0' + i % 10), 0};
        CHECK(fs_write(name, "group", 5) == 0);
    }
    CHECK(fs_init() == 0); CHECK(fs_size("/many/f159") == 5);
    CHECK(fs_write("/many/f159", "updated", 7) == 0);
    CHECK(fs_read("/many/f159", 0, result, 7) == 7 && !memcmp(result, "updated", 7));
    CHECK(get32(SB + 12) < initial_free);
    CHECK(fs_sync() == 0); CHECK(fclose(disk_stream) == 0);
    puts("5 GiB ext2, high block addresses and multiple inode groups passed");
    return 0;
}
