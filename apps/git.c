#include "command.h"

static int same(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0; }
static void repo_name(const char *url, char *out, int limit)
{
    const char *p = url, *last = url; int n = 0;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    while (*last && *last != '.' && n + 1 < limit) out[n++] = *last++;
    out[n] = 0;
}
int main(int argc, const char *const *argv)
{
    char destination[96];
    if (argc != 3 || !same(argv[1], "clone")) { barnix->puts("Usage: git clone https://PATH"); return 1; }
    repo_name(argv[2], destination, sizeof(destination));
    if (!barnix->net_download(argv[2], destination)) { barnix->puts("Repository cloned"); return 0; }
    barnix->puts("Clone failed: Git transport or network adapter is unavailable");
    return 1;
}
