#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv; if (argc != 1) return fail("usage: sync");
    if (barnix->sync()) return 1;
    barnix->println(2, barnix->translate("filesystem saved")); return 0;
}
