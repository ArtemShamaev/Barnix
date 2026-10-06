#define main original_main
#include "fs_test.c"
#undef main
static int has_output(const char *text)
{
    for (const char *p = output; *p; p++) if (!strncmp(p, text, strlen(text))) return 1;
    return 0;
}
static void check_listing(const char *expected)
{
    DiskSelection before = disk_selection();
    unsigned int old_cwd = cwd;
    int old_volume = active_volume, old_mounted = mounted, writes = write_calls;
    output[0] = 0; fs_lsblk();
    if(!has_output(expected)){puts(expected);puts(output);}
    CHECK(has_output(expected));
    DiskSelection after = disk_selection();
    CHECK(!memcmp(&before, &after, sizeof(before)));
    CHECK(cwd == old_cwd && active_volume == old_volume && mounted == old_mounted);
    CHECK(write_calls == writes);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    void *file = fopen(argv[1], "rb"); CHECK(file);
    CHECK(fread(test_disk, 1, sizeof(test_disk), file) == sizeof(test_disk)); fclose(file);
    CHECK(fs_init() == 0);
    check_listing("disk2   disk 2048      -       -         -         -\n");
    CHECK(!namespace_active);
    CHECK(fs_format("ram0") == -1);
    CHECK(fs_format("disk2") == 0);
    CHECK(fs_format("disk3") == 0);
    check_listing("disk2   disk 2048      ext2    22        2026      -\n");
    /* Invalid free counts must not wrap, and unsupported filesystems must
     * not be mistaken for ext2 just because they share its magic. */
    put32(test_disks[1][2] + 12, 2049);
    check_listing("disk2   disk 2048      ext2    -         -         -\n");
    put32(test_disks[1][2] + 12, 2026);
    put32(test_disks[1][2] + 76, 1); put32(test_disks[1][2] + 96, 0x40);
    check_listing("disk2   disk 2048      -       -         -         -\n");
    put32(test_disks[1][2] + 76, 1); put32(test_disks[1][2] + 96, 2);
    CHECK(fs_mkdir("one") == 0); CHECK(fs_mkdir("two") == 0);
    CHECK(fs_mount("disk2", "/one") == 0);
    CHECK(fs_mount("disk3", "/two") == 0);
    CHECK(fs_format("disk2") == -1);
    CHECK(fs_write("/one/file", "first", 5) == 0);
    CHECK(fs_write("/two/file", "second", 6) == 0);
    CHECK(fs_write("/rootfile", "root", 4) == 0);
    check_listing("disk2   disk 2048      ext2    23        2025      /one\n");
    CHECK(has_output("disk3   disk 2048      ext2    23        2025      /two\n"));
    expect_cat("/one/file", "first\n"); expect_cat("/two/file", "second\n");
    CHECK(fs_cd("/one") == 0);
    CHECK(fs_size("../two/file") == 6);
    CHECK(fs_append("file", "!", 1) == 0);
    char path[64]; CHECK(fs_getcwd(path, sizeof(path)) == 5); CHECK(!strcmp(path, "/one"));
    CHECK(fs_cp("file", "../two/copy") == 0);
    expect_cat("/two/copy", "first!\n");
    CHECK(fs_cd("..") == 0); CHECK(fs_size("rootfile") == 4);
    CHECK(fs_rmdir("/one") == -1);
    CHECK(fs_mount("disk2", "/two") == -1);
    CHECK(fs_mount("missing", "/one") == -1);
    CHECK(fs_size("/one/file") == 6);
    CHECK(fs_unmount_path("/one") == 0);
    CHECK(fs_size("/one/file") == -1);
    check_listing("disk2   disk 2048      ext2    23        2025      -\n");
    CHECK(fs_mount("disk2", "/one") == 0);
    expect_cat("/one/file", "first!\n");
    CHECK(fs_mkdir("/one/nested") == 0);
    CHECK(fs_unmount_path("/two") == 0);
    CHECK(fs_mount("disk3", "/one/nested") == 0);
    CHECK(fs_unmount_path("/one") == -1);
    CHECK(fs_cd("/one/nested") == 0); CHECK(fs_cd("..") == 0);
    CHECK(fs_getcwd(path, sizeof(path)) == 5 && !strcmp(path, "/one"));
    CHECK(fs_unmount_path("/one/nested") == 0);
    CHECK(fs_unmount_path("/one") == 0);
    CHECK(fs_format("disk2") == 0);
    CHECK(fs_mount("disk2", "/one") == 0); CHECK(fs_size("/one/file") == -1);
    CHECK(fs_write("/one/new", "fresh", 5) == 0);
    CHECK(fs_sync() == 0);
    CHECK(disk_select("disk2") == 0);
    file = fopen(argv[1], "wb"); CHECK(file);
    CHECK(fwrite(test_disk, 1, sizeof(test_disk), file) == sizeof(test_disk)); fclose(file);
    puts("Mount routing and format tests passed");
    return 0;
}
