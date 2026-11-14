/* beep [hz] [ms]: a PC speaker tone (default 880 Hz for 200 ms). */
#include "command.h"
#include <barnix_sound.h>

static int number(const char *s, unsigned int *out)
{
    unsigned int value = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || value > 100000) return -1;
        value = value * 10 + (unsigned int)(*s - '0');
    }
    *out = value;
    return 0;
}

int main(int argc, const char *const *argv)
{
    unsigned int hz = 880, ms = 200;
    if (argc > 3 || (argc > 1 && number(argv[1], &hz)) || (argc > 2 && number(argv[2], &ms)) ||
        hz < 20 || hz > 20000 || ms > 10000)
        return fail("usage: beep [hz 20..20000] [ms 0..10000]");
    while (ms) {
        unsigned int part = ms > 1000 ? 1000 : ms;   /* sleep_ms is capped at one second */
        barnix->speaker(hz);
        barnix->sleep_ms(part);
        ms -= part;
    }
    barnix->speaker(0);
    return 0;
}
