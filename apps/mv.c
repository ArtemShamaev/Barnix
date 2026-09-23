#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 3) return fail("usage: mv <source> <destination>");
    return result(barnix->mv(argv[1], argv[2]));
}
