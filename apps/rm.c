#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: rm <name>");
    return result(barnix->rm(argv[1]));
}
