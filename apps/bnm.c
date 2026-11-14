/* bnm: Barnix network manager. Shows the adapter and address, connects
 * by cable (DHCP) or Wi-Fi, lists Wi-Fi networks, turns networking off,
 * pings and resolves names. */
#include "command.h"

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}
static void number(int color, unsigned int value)
{
    char text[12]; int n = 0;
    do text[n++] = (char)('0' + value % 10); while (value /= 10);
    char out[12]; int k = 0;
    while (n) out[k++] = text[--n];
    out[k] = 0;
    barnix->print(color, out);
}
static void address(int color, unsigned int ip)
{
    for (int i = 3; i >= 0; i--) { number(color, ip >> (8 * i) & 255); if (i) barnix->print(color, "."); }
}
static void hex2(unsigned int v)
{
    char text[3] = {"0123456789abcdef"[v >> 4 & 15], "0123456789abcdef"[v & 15], 0};
    barnix->print(15, text);
}
static const char *problem(int code)
{
    switch (code) {
    case BARNIX_NET_NO_ADAPTER: return "no supported network adapter";
    case BARNIX_NET_NO_LINK: return "not connected: the cable is unplugged or the Wi-Fi network is gone";
    case BARNIX_NET_NO_ADDRESS: return "no answer from a DHCP server";
    case BARNIX_NET_NO_NAME: return "cannot find that host name";
    case BARNIX_NET_TIMEOUT: return "the access point does not answer";
    case BARNIX_NET_NO_DRIVER: return "Barnix has no driver for this Wi-Fi card yet";
    case BARNIX_NET_NOT_FOUND: return "no Wi-Fi network with that name (bnm get-networks)";
    case BARNIX_NET_WRONG_KEY: return "wrong Wi-Fi password";
    case BARNIX_NET_REJECTED: return "the access point refused the connection";
    case BARNIX_NET_UNSUPPORTED: return "the network uses security Barnix does not support (WPA3, WEP, Enterprise)";
    case BARNIX_NET_OFF: return "networking is turned off (bnm internet or bnm connect)";
    default: return "network error";
    }
}
static int failed(const char *what, int code)
{
    barnix->print(4, barnix->translate(what)); barnix->println(4, barnix->translate(problem(code)));
    return 1;
}

/* The scan of net_scan: lines of "*name\tsecurity\tsignal" (see net.c). */
static char networks[2048];
static int scan(void)
{
    int count = barnix->net_scan(networks, sizeof(networks));
    if (count < 0) networks[0] = 0;
    return count;
}
static const char *field(const char *line, int index, char *out, unsigned int capacity)
{
    unsigned int n = 0;
    for (; index; line++) { if (*line == '\t') index--; else if (!*line || *line == '\n') { out[0] = 0; return out; } }
    while (*line && *line != '\t' && *line != '\n' && n + 1 < capacity) out[n++] = *line++;
    out[n] = 0;
    return out;
}
static const char *connected_network(char *out, unsigned int capacity)
{
    out[0] = 0;
    for (const char *line = networks; *line;) {
        if (*line == '*') { field(line + 1, 0, out, capacity); break; }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return out;
}

static int status(void)
{
    BarnixNetInfo info;
    if (barnix->net_info(&info))
        return fail("no supported network adapter (Intel PRO/1000, Realtek 8139/8111/8168, AMD PCnet, Barnix virtual Wi-Fi)");
    barnix->print(15, "Adapter:  "); barnix->println(11, info.adapter);
    barnix->print(15, "MAC:      ");
    for (int i = 0; i < 6; i++) { hex2(info.mac[i]); if (i < 5) barnix->print(15, ":"); }
    barnix->puts("");
    if (info.state == 1) {
        char name[40];
        scan();
        barnix->print(15, "Wi-Fi:    ");
        barnix->print(10, barnix->translate("connected to ")); barnix->println(10, connected_network(name, sizeof(name)));
    } else if (info.state == 2) {
        barnix->print(15, "Cable:    "); barnix->println(10, barnix->translate("connected"));
    } else {
        barnix->print(15, "Network:  "); barnix->println(12, barnix->translate("off"));
        barnix->println(14, barnix->translate("Connect: bnm internet (cable) or bnm connect WIFI [PASSWORD]"));
        return 0;
    }
    barnix->print(15, "Address:  "); address(10, info.address); barnix->print(15, " / "); address(15, info.mask); barnix->puts("");
    barnix->print(15, "Gateway:  "); address(15, info.gateway); barnix->puts("");
    barnix->print(15, "DNS:      "); address(15, info.dns); barnix->puts("");
    return 0;
}
static int list_networks(void)
{
    barnix->puts(barnix->translate("Looking for Wi-Fi networks..."));
    int count = scan();
    if (count < 0) return failed("Wi-Fi: ", count);
    if (!count) { barnix->println(14, barnix->translate("No Wi-Fi networks found")); return 0; }
    for (const char *line = networks; *line;) {
        char name[40], security[32], signal[8];
        int current = *line == '*';
        field(line + 1, 0, name, sizeof(name)); field(line + 1, 1, security, sizeof(security)); field(line + 1, 2, signal, sizeof(signal));
        barnix->print(10, current ? "* " : "  ");
        barnix->print(current ? 10 : 15, name);
        for (unsigned int n = strlen(name); n < 34; n++) barnix->print(15, " ");
        barnix->print(security[0] == 'o' || security[0] == 'W' ? 11 : 12, security);
        barnix->print(15, "  "); barnix->print(7, signal); barnix->println(7, " dBm");
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return 0;
}
/* Typed without echo: a star per character. */
static void ask_password(char *out, unsigned int capacity)
{
    unsigned int n = 0;
    barnix->print(15, barnix->translate("Password: "));
    for (;;) {
        unsigned int key = barnix->getch();
        if (key == '\n' || key == '\r') break;
        if (key == '\b') { if (n) { n--; barnix->print(15, "\b \b"); } continue; }
        if (key >= 32 && key < 127 && n + 1 < capacity) { out[n++] = (char)key; barnix->print(15, "*"); }
    }
    out[n] = 0;
    barnix->puts("");
}
static int connect(const char *name, const char *password)
{
    char typed[64];
    if (!password) {
        int count = scan(), secured = 1;
        for (const char *line = networks; count > 0 && *line;) {
            char found[40], security[32];
            field(line + 1, 0, found, sizeof(found)); field(line + 1, 1, security, sizeof(security));
            if (same(found, name) && same(security, "open")) secured = 0;
            while (*line && *line != '\n') line++;
            if (*line) line++;
        }
        if (secured) { ask_password(typed, sizeof(typed)); password = typed; }
    }
    barnix->print(15, barnix->translate("Connecting to ")); barnix->print(11, name); barnix->puts("...");
    int result = barnix->net_connect_wifi(name, password);
    for (unsigned int i = 0; i < sizeof(typed); i++) typed[i] = 0;
    if (result) return failed("Unable to connect: ", result);
    return status();
}
static int choose_network(void)
{
    barnix->puts(barnix->translate("Looking for Wi-Fi networks..."));
    int count = scan();
    if (count < 0) return failed("Wi-Fi: ", count);
    if (!count) return fail("No Wi-Fi networks found");
    int shown = 0;
    for (const char *line = networks; *line && shown < 9;) {
        char name[40], security[32];
        field(line + 1, 0, name, sizeof(name));
        field(line + 1, 1, security, sizeof(security));
        number(11, ++shown); barnix->print(15, ") ");
        barnix->print(15, name); barnix->print(8, "  "); barnix->println(8, security);
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    barnix->print(15, barnix->translate("Choose network (1-9, Esc cancels): "));
    unsigned int key;
    do { key = barnix->getch(); } while (key != KEY_ESCAPE && (key < '1' || key >= '1' + (unsigned int)shown));
    if (key == KEY_ESCAPE) { barnix->puts(""); return 0; }
    barnix->print(15, (const char[]){(char)key, 0}); barnix->puts("");
    int selected = (int)(key - '1');
    for (const char *line = networks; *line;) {
        if (!selected--) {
            char name[40]; field(line + 1, 0, name, sizeof(name));
            return connect(name, 0);
        }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return 1;
}
int main(int argc, const char *const *argv)
{
    if (barnix->version < 12) return fail("bnm needs a Barnix kernel with networking (ABI 12)");
    if (argc == 1 || (argc == 2 && same(argv[1], "status"))) return status();
    if (argc == 2 && same(argv[1], "connect")) return choose_network();
    if ((argc == 3 || argc == 4) && same(argv[1], "connect")) return connect(argv[2], argc == 4 ? argv[3] : 0);
    if (argc == 2 && same(argv[1], "get-networks")) return list_networks();
    if (argc == 2 && same(argv[1], "off")) {
        int result = barnix->net_connect_wifi(0, 0);
        if (result) return failed("", result);
        barnix->println(14, barnix->translate("Networking is off: cable and Wi-Fi are disconnected"));
        barnix->println(7, barnix->translate("Turn it on again: bnm internet or bnm connect WIFI [PASSWORD]"));
        return 0;
    }
    if (argc == 2 && same(argv[1], "internet")) {
        barnix->puts(barnix->translate("Asking for an address (DHCP)..."));
        int result = barnix->net_connect_cable();
        if (result) return failed("Unable to connect: ", result);
        return status();
    }
    if (argc == 3 && same(argv[1], "resolve")) {
        unsigned int ip;
        int result = barnix->net_resolve(argv[2], &ip);
        if (result) return failed("Cannot resolve: ", result);
        barnix->print(15, argv[2]); barnix->print(15, " = "); address(10, ip); barnix->puts("");
        return 0;
    }
    if ((argc == 2 || argc == 3) && same(argv[1], "test-ping")) {
        unsigned int ip = 0, ms;
        BarnixNetInfo info;
        if (argc == 3) {
            int result = barnix->net_resolve(argv[2], &ip);
            if (result) return failed("Cannot resolve: ", result);
        } else {
            int result = barnix->net_info(&info);
            if (!result && info.state != 1 && info.state != 2) {   /* connects as programs do, unless turned off */
                barnix->net_ping(&ms);
                result = barnix->net_info(&info);
            }
            if (result || (info.state != 1 && info.state != 2))
                return fail("network is unavailable (turned off, or no cable and no Wi-Fi network)");
            ip = info.gateway;
        }
        barnix->print(15, "Ping "); address(15, ip); barnix->print(15, "... ");
        for (int i = 0; i < 4; i++) {
            if (!barnix->net_ping_host(ip, &ms)) { barnix->print(10, "reply in "); number(10, ms); barnix->println(10, " ms"); return 0; }
        }
        barnix->println(12, "no reply");
        return 1;
    }
    barnix->puts("usage: bnm [status] | internet | connect [WIFI [PASSWORD]] | get-networks | off | test-ping [HOST] | resolve HOST");
    return 1;
}
