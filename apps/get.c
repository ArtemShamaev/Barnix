/* get URL [FILE]: downloads over HTTP or HTTPS (BDK Barnix Net) into a
 * file; without FILE the name comes from the URL. */
#include "command.h"
#include <barnix_net.h>

#define LIMIT (16U * 1024U * 1024U)   /* the Barnix file size limit */
static unsigned char *buffer;
static unsigned int used, capacity, marks;
static int too_big;

static void print_number(int color, unsigned int value)
{
    char text[12]; int n = 0;
    do text[n++] = (char)('0' + value % 10); while (value /= 10);
    char out[12]; int k = 0;
    while (n) out[k++] = text[--n];
    out[k] = 0;
    barnix->print(color, out);
}
static int store(void *context, const void *data, unsigned int length)
{
    (void)context;
    if (used + length > capacity) { too_big = 1; return 1; }
    const unsigned char *p = data;
    for (unsigned int i = 0; i < length; i++) buffer[used + i] = p[i];
    used += length;
    while (used / (256 * 1024) > marks) { barnix->print(10, "#"); marks++; }
    return 0;
}
static void name_from(const char *url, char *out, unsigned int capacity_out)
{
    const char *start = url, *end;
    for (const char *p = url; *p && *p != '?' && *p != '#'; p++) if (*p == '/') start = p + 1;
    end = start;
    while (*end && *end != '?' && *end != '#') end++;
    if (start == end || (start > url && start[-1] == '/' && start - url < 9)) {   /* no path: the site's page */
        const char *text = "index.html";
        unsigned int n = 0;
        while (text[n]) { out[n] = text[n]; n++; }
        out[n] = 0;
        return;
    }
    unsigned int n = 0;
    while (start < end && n + 1 < capacity_out) out[n++] = *start++;
    out[n] = 0;
}
int main(int argc, const char *const *argv)
{
    if (argc < 2 || argc > 3) return fail("usage: get https://host/path [FILE]");
    if (barnix->version < 12) return fail("get needs a Barnix kernel with networking (ABI 12)");
    char destination[256];
    if (argc == 3) {
        unsigned int n = 0;
        while (argv[2][n] && n + 1 < sizeof(destination)) { destination[n] = argv[2][n]; n++; }
        destination[n] = 0;
    } else name_from(argv[1], destination, sizeof(destination));
    unsigned int bytes = LIMIT + 1;
    buffer = barnix->memory(&bytes);
    if (!buffer || bytes < LIMIT + 1) return fail("not enough memory");
    capacity = LIMIT;
    barnix->print(15, "Connecting... ");
    static BnetRequest request;
    request.data = store;
    int result = bnet_request(argv[1], &request);
    barnix->puts("");
    if (too_big) return fail("the file is larger than 16 MiB, the Barnix file size limit");
    if (result) { barnix->print(4, "Download failed: "); barnix->println(4, request.error); return 1; }
    if (barnix->write(destination, (const char *)buffer, (int)used)) return fail("cannot write the file (name, permissions or space)");
    barnix->print(15, "Saved ");
    barnix->print(11, destination);
    barnix->print(15, " (");
    print_number(15, used);
    barnix->println(15, " bytes)");
    return 0;
}
