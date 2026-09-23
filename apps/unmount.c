#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv; if (argc != 1) return fail("usage: unmount");
    if (barnix->unmount()) return fail("unmount failed; check device and sync");
    barnix->println(2, barnix->translate("filesystem unmounted; device may be removed")); return 0;
}
