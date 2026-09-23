#include "command.h"

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int password(char *out, int limit)
{
    int n = 0;
    barnix->print(15, "Password for wi-fi: ");
    while (n + 1 < limit) {
        unsigned int key = barnix->getch();
        if (key == '\n') break;
        if (key == 0x08) { if (n) n--; continue; }
        if (key >= 32 && key < 127) { out[n++] = (char)key; barnix->print(15, "*"); }
    }
    out[n] = 0; barnix->puts(""); return n;
}

static void print_number(unsigned int value)
{
    char text[12]; int n = 0, i;
    do { text[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (i = n - 1; i >= 0; i--) { char one[2] = {text[i], 0}; barnix->print(15, one); }
}

int main(int argc, const char *const *argv)
{
    char pass[80], networks[512]; unsigned int ms;
    if (argc == 3 && same(argv[1], "connect")) {
        password(pass, sizeof(pass));
        if (!barnix->net_connect_wifi(argv[2], pass)) { barnix->puts("Connected"); return 0; }
        barnix->puts("Unable to connect: network adapter or Wi-Fi driver is unavailable"); return 1;
    }
    if (argc == 2 && same(argv[1], "internet")) {
        if (!barnix->net_connect_cable()) { barnix->puts("Connected"); return 0; }
        barnix->puts("Unable to connect: network adapter is missing or link is down"); return 1;
    }
    if (argc == 2 && same(argv[1], "get-networks")) {
        int result = barnix->net_scan(networks, sizeof(networks));
        if (result < 0) { barnix->puts("Unable to scan: Wi-Fi driver is unavailable"); return 1; }
        barnix->print(15, networks); return 0;
    }
    if (argc == 2 && same(argv[1], "test-ping")) {
        barnix->puts("Wait...");
        if (!barnix->net_ping(&ms)) { barnix->print(15, "Done, "); print_number(ms); barnix->puts("ms"); return 0; }
        barnix->puts("Network is unavailable"); return 1;
    }
    barnix->puts("Usage: bnm connect WIFI_NAME | internet | get-networks | test-ping");
    return 1;
}
