#include "command.h"

static void basename(const char *url, char *out, int limit)
{
    const char *p = url, *last = url; int n = 0;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    while (last[n] && n + 1 < limit) { out[n] = last[n]; n++; }
    if (!n) { out[0] = 'd'; out[1] = 'o'; out[2] = 'w'; out[3] = 'n'; out[4] = 0; }
    else out[n] = 0;
}
int main(int argc, const char *const *argv)
{
    char destination[128];
    if (argc < 2 || argc > 3) { barnix->puts("Usage: get https://PATH [FILE]"); return 1; }
    if (argc == 3) {
        int i = 0; while (argv[2][i] && i + 1 < (int)sizeof(destination)) { destination[i] = argv[2][i]; i++; }
        destination[i] = 0;
    } else basename(argv[1], destination, sizeof(destination));
    if (!barnix->net_download(argv[1], destination)) { barnix->puts("Downloaded"); return 0; }
    barnix->puts("Download failed: network adapter or Internet stack is unavailable");
    return 1;
}
