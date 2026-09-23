#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: mkdir <name>");
    return result(barnix->mkdir(argv[1]));
}
