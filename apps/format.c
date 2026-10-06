#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: format DRIVE");
    if (barnix->format(argv[1])) return fail("format failed: unmount drive first; requires at least 2 MiB");
    barnix->puts(barnix->translate("formatted: empty ext2 filesystem"));
    return 0;
}
