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
    else if (barnix->net_state() == 2) barnix->puts("network: connected (cable)");
    else if (barnix->net_state() == 1) barnix->puts("network: connected (Wi-Fi)");
    else barnix->puts("network: adapter detected, disconnected");
    char line[96] = "cpu: ", *p = line + 5;
    unsigned int cpus = barnix->cpus(), digits = 1;
    for (unsigned int n = cpus; n >= 10; n /= 10) digits++;
    for (unsigned int i = digits, n = cpus; i; n /= 10) p[--i] = (char)('0' + n % 10);
    p += digits;
    for (const char *t = cpus == 1 ? " processor core" : " processor cores"; *t;) *p++ = *t++;
    *p = 0;
    barnix->puts(line);
    char device[64];
    int sound = barnix->sound_device(device, sizeof(device));
    p = line;
    for (const char *t = "sound: "; *t;) *p++ = *t++;
    for (const char *t = device; *t;) *p++ = *t++;
    if (sound != BARNIX_SOUND_CARD) for (const char *t = " only (no sound card)"; *t;) *p++ = *t++;
    *p = 0;
    barnix->puts(line);
    return 0;
}
