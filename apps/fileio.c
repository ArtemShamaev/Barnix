#include "barnix_app.h"
static unsigned char contents[32768];
static unsigned char received[32768];
int main(int argc, const char *const *argv)
{
    (void)argc; (void)argv;
    for (unsigned int i = 0; i < sizeof(contents); i++) contents[i] = i % 251;
    if (barnix->write("program.dat", (const char *)contents, sizeof(contents)) ||
        barnix->size("program.dat") != sizeof(contents) ||
        barnix->read("program.dat", 0, received, sizeof(received)) != sizeof(received))
        return 1;
    for (unsigned int i = 0; i < sizeof(contents); i++)
        if (contents[i] != received[i]) return 2;
    barnix->puts("ELF file I/O: 32768 bytes verified");
    return 0;
}
