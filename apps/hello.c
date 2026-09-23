#include "barnix_app.h"
/* These force real initialized data and BSS segments, including an ELF file
 * larger than ext2's 12 direct blocks. Every run must reload/zero them. */
static volatile unsigned char initialized[16384] = { [0] = 90, [16383] = 165 };
static volatile unsigned char zeros[8192];
int main(int argc, const char *const *argv)
{
    if (initialized[0] != 90 || initialized[16383] != 165) return 91;
    for (unsigned int i = 0; i < sizeof(zeros); i++) if (zeros[i]) return 92;
    initialized[0] = 0; zeros[8191] = 1;
    barnix->puts("Hello from GCC ELF!");
    barnix->puts("ELF data/BSS verified");
    for (int i = 1; i < argc; i++) barnix->puts(argv[i]);
    return 7;
}
