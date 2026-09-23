#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv; if (argc != 1) return fail("usage: diskinfo"); barnix->disk_info(); return 0;
}
