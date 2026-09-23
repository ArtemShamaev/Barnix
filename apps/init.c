#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv;
    if (argc != 1) return fail("usage: init");
    if (barnix->init()) return 1;
    barnix->puts(barnix->translate("configuration applied"));
    return 0;
}
