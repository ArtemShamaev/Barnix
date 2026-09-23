#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv; if (argc != 1) return fail("usage: clear"); barnix->clear(); return 0;
}
