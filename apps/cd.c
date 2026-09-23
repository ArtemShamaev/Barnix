#include "command.h"
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: cd <name>");
    return result(barnix->cd(argv[1]));
}
