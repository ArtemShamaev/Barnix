#include "command.h"
static int is_lsblk(const char *s)
{
    const char *name = "lsblk";
    while (*name && *s == *name) { s++; name++; }
    return !*s && !*name;
}
int main(int argc, const char *const *argv)
{
    if (argc == 2 && is_lsblk(argv[1])) { barnix->lsblk(); return 0; }
    if (argc != 1) return fail("usage: devices [lsblk]");
    barnix->devices();
    if (barnix->net_state() < 0) barnix->puts("network: no supported adapter");
    else if (barnix->net_state() == 2) barnix->puts("network: connected");
    else barnix->puts("network: adapter detected, disconnected");
    return 0;
}
