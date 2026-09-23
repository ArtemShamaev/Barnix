#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: stat <name>");
    return result(barnix->stat(argv[1]));
}
