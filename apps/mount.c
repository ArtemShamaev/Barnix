#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc == 1) { barnix->mount_info(); return 0; }
    if (argc != 3) return fail("usage: mount <usb0|ata0|ram0> <mountpoint>");
    if (barnix->mount(argv[1], argv[2])) return fail("mount failed (device, ext2 or mountpoint unsupported)");
    barnix->mount_info(); return 0;
}
