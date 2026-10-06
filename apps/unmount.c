#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc > 2) return fail("usage: unmount [mountpoint]");
    if (barnix->unmount_path(argc == 2 ? argv[1] : 0)) return fail("unmount failed; check device and sync");
    barnix->println(2, barnix->translate("filesystem unmounted; device may be removed")); return 0;
}
