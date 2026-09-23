#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: rmdir <name>");
    return result(barnix->rmdir(argv[1]));
}
