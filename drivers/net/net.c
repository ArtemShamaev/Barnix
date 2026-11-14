/* Barnix networking core: adapter discovery, Ethernet, ARP, IPv4, ICMP,
 * UDP, DHCP and DNS over the cable or Wi-Fi (wifi.c). Everything is polled: net_poll() drains the adapter
 * whenever a caller waits for an answer. TCP lives in net_tcp.c. */
#include "net.h"
#include "net_driver.h"
#include "wifi.h"
#include "barnix.h"
#include "usb_time.h"
#include "fs.h"

typedef unsigned char u8;
static NetDriver driver, cable;   /* driver: the link the stack uses now */
static int adapter, have_cable, state, configured, off;
/* The network chosen last, so programs reconnect to it by themselves */
static int wifi_chosen;
static char wifi_name[33], wifi_password[64];
static unsigned int my_ip, my_mask, my_gateway, my_dns;
static u8 frame_out[1600], frames_in[2][1600];
static unsigned int tsc_per_ms;

/* ------------------------------------------------------------ helpers */
static inline void outl(unsigned short port, unsigned int value) { __asm__ volatile("outl %0,%1" : : "a"(value), "Nd"(port)); }
static inline unsigned int inl(unsigned short port) { unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port)); return v; }
unsigned int net_pci_read(unsigned int pci, unsigned int reg) { outl(0xcf8, 0x80000000U | pci | (reg & 0xfc)); return inl(0xcfc); }
void net_pci_write(unsigned int pci, unsigned int reg, unsigned int value) { outl(0xcf8, 0x80000000U | pci | (reg & 0xfc)); outl(0xcfc, value); }
void net_delay_ms(unsigned int ms) { usb_delay_ms(ms); }
/* Milliseconds since power-on, modulo 2^32: the whole 64-bit counter is
 * divided, so differences stay right across the wrap (a 32-bit scaled
 * counter wrapped every 24 minutes at 3 GHz and timed everything out). */
unsigned int net_ms(void)
{
    if (!tsc_per_ms) return 0;
    unsigned int lo, hi, ms, rest;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    __asm__("divl %4" : "=a"(ms), "=d"(rest) : "a"(lo), "d"(hi % tsc_per_ms), "rm"(tsc_per_ms));
    (void)rest;
    return ms;
}
static void put16(u8 *p, unsigned int v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, unsigned int v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static unsigned int get16(const u8 *p) { return (unsigned int)p[0] << 8 | p[1]; }
static unsigned int get32(const u8 *p) { return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3]; }
unsigned int net_checksum_add(unsigned int sum, const void *data, unsigned int length)
{
    const u8 *p = data;
    while (length > 1) { sum += (unsigned int)p[0] << 8 | p[1]; p += 2; length -= 2; }
    if (length) sum += (unsigned int)p[0] << 8;
    return sum;
}
unsigned short net_checksum_end(unsigned int sum)
{
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}
unsigned int net_address(void) { return my_ip; }

/* Entropy: the time stamp counter around port I/O jitters; RDRAND when the
 * processor has it. Programs hash what they get (BDK net.c). */
void net_random(void *out, unsigned int length)
{
    unsigned int a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    int rdrand = (c >> 30) & 1;
    u8 *p = out;
    static unsigned int mix = 0x9e3779b9U;
    for (unsigned int i = 0; i < length; i++) {
        unsigned int lo, hi, value = 0;
        for (int k = 0; k < 4; k++) {
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            __asm__ volatile("outb %%al,$0x43; inb $0x40,%%al" ::: "eax");   /* PIT read: a slow, jittery bus cycle */
            value = (value << 7 | value >> 25) ^ lo;
        }
        if (rdrand) { unsigned int r = 0; __asm__ volatile("1: rdrand %0; jnc 1b" : "=r"(r)); value ^= r; }
        mix = (mix ^ value) * 0x01000193U + i;
        p[i] = (u8)(mix >> 24 ^ mix);
    }
}

/* ------------------------------------------------------------ adapter */
void net_init(void)
{
    adapter = have_cable = 0; state = NET_DISCONNECTED; configured = 0;
    {   /* calibrate the clock */
        tsc_per_ms = timer_tsc_per_ms();
        if (!tsc_per_ms) tsc_per_ms = 1024;
    }
    /* The first wired adapter and the first Wi-Fi card. Each driver keeps
     * one card's state, so it is not probed again once it runs one. */
    typedef int (*Probe)(unsigned int, unsigned int, NetDriver *);
    static const Probe probes[4] = {e1000_probe, rtl8139_probe, rtl8169_probe, pcnet_probe};
    int used[4] = {0, 0, 0, 0}, radio = 0;
    for (unsigned int bus = 0; bus < 256 && !(have_cable && radio); bus++)
        for (unsigned int slot = 0; slot < 32; slot++)
            for (unsigned int fn = 0; fn < 8; fn++) {
                unsigned int pci = bus << 16 | slot << 11 | fn << 8, id = net_pci_read(pci, 0);
                if ((id & 0xffff) == 0xffff) { if (!fn) break; continue; }
                unsigned int class = net_pci_read(pci, 8) >> 16;
                if (class == 0x0280 && !radio) radio = wifi_identify(pci, id);
                for (int i = 0; i < 4 && class == 0x0200; i++) {
                    NetDriver found;
                    if (used[i] || probes[i](pci, id, &found)) continue;
                    used[i] = 1;
                    if (!radio && !vwifi_probe(&found)) radio = 1;
                    else if (!have_cable) { cable = found; have_cable = 1; }
                    break;
                }
                if (!fn && !(net_pci_read(pci, 12) & 0x800000)) break;
            }
    driver = cable;
    adapter = have_cable || radio;
}
int net_state(void) { return adapter ? state : -1; }
static void unconfigure(void)
{
    net_reset();
    configured = 0; state = NET_DISCONNECTED;
    my_ip = my_mask = my_gateway = my_dns = 0;
}
static void use_cable(void) { driver = cable; }
/* Turns networking off until a program connects again on purpose
 * (net_connect_cable, net_connect_wifi with a name). */
static int net_off(void)
{
    wifi_disconnect();
    unconfigure();
    use_cable();
    off = 1;
    wifi_chosen = 0;
    memset(wifi_password, 0, sizeof(wifi_password));
    return adapter ? 0 : BARNIX_NET_NO_ADAPTER;
}
static int dhcp(void);
int net_connect_wifi(const char *name, const char *password)
{
    if (!name) return net_off();
    if (!wifi_adapter()) return BARNIX_NET_NO_ADAPTER;
    if (!wifi_ready()) return BARNIX_NET_NO_DRIVER;
    unconfigure();
    off = 0;
    NetDriver radio;
    int result = wifi_connect(name, password, &radio);
    if (!result) {
        driver = radio;
        result = dhcp();
        if (result) wifi_disconnect();
    }
    if (result) { use_cable(); return result; }
    state = NET_WIFI;
    wifi_chosen = 1;
    if (name != wifi_name) {
        strcpy(wifi_name, name);
        wifi_password[0] = 0;
        if (password && strlen(password) < (int)sizeof(wifi_password)) strcpy(wifi_password, password);
    }
    return 0;
}
/* Networks in range, one per line: "*" for the one we are in (else a
 * space), the name, a tab, the security ("open", "WPA2" or "WPA3
 * (unsupported)" and so on), a tab and the signal in dBm. The count, or
 * a negative BARNIX_NET_* error. */
int net_scan(char *out, unsigned int size)
{
    static WifiBss list[24];
    if (out && size) out[0] = 0;
    int count = wifi_scan(list, 24);
    if (count <= 0) return count;
    unsigned int at = 0;
    for (int i = 0; i < count && out; i++) {
        char line[96], number[8];
        int signal = list[i].rssi, n = 0;
        unsigned int length = 0;
        const char *ssid = wifi_ssid();
        line[length++] = ssid && !strcmp(ssid, list[i].ssid) ? '*' : ' ';
        strcpy(line + length, list[i].ssid); length += strlen(list[i].ssid);
        line[length++] = '\t';
        const char *security = list[i].security == WIFI_OPEN ? "open" : list[i].security == WIFI_WPA2 ? "WPA2" : list[i].why;
        strcpy(line + length, security); length += strlen(security);
        if (list[i].security == WIFI_UNSUPPORTED) { strcpy(line + length, " (unsupported)"); length += 14; }
        line[length++] = '\t';
        if (signal < 0) { line[length++] = '-'; signal = -signal; }
        do number[n++] = (char)('0' + signal % 10); while (signal /= 10);
        while (n) line[length++] = number[--n];
        line[length++] = '\n';
        if (at + length + 1 > size) break;
        memcpy(out + at, line, (int)length);
        at += length;
        out[at] = 0;
    }
    return count;
}
/* Small, streaming HTTP/1.1 client for installer packages. The caller must
 * validate downloaded bytes before treating them as code. */
int net_http_get(const char *url, unsigned int maximum, NetBodySink sink, void *context)
{
    if (!url || !sink || !starts_with(url, "http://")) return -1;
    const char *authority = url + 7, *path = strchr(authority, '/');
    if (!path || path - authority < 1 || path - authority > 90) return -1;
    char host[96]; memcpy(host, authority, path - authority); host[path - authority] = 0;
    unsigned int port = 80;
    char *colon = strchr(host, ':');
    if (colon) {
        *colon++ = 0;
        int parsed = atoi(colon);
        if (parsed <= 0 || parsed > 65535) return -1;
        port = (unsigned int)parsed;
    }
    if (!*host || strlen(path) > 200) return -1;
    int handle = net_open(host, port);
    if (handle < 0) return handle;
    char request[400];
    strcpy(request, "GET "); strcat(request, path);
    strcat(request, " HTTP/1.1\r\nHost: "); strcat(request, host);
    strcat(request, "\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n");
    int result = -1;
    if (net_send(handle, request, strlen(request)) < 0) goto done;
    char header[4096]; unsigned int used = 0, length = 0, written = 0;
    int header_done = 0;
    for (;;) {
        unsigned char buffer[2048];
        int got = net_recv(handle, buffer, sizeof(buffer), 10000);
        if (got <= 0) { if (got == 0 && header_done && written == length) result = 0; break; }
        unsigned int offset = 0;
        if (!header_done) {
            while (offset < (unsigned int)got && used < sizeof(header) - 1) {
                header[used++] = buffer[offset++];
                if (used >= 4 && !memcmp(header + used - 4, "\r\n\r\n", 4)) {
                    header_done = 1; break;
                }
            }
            if (!header_done) {
                if (used >= sizeof(header) - 1) break;
                continue;
            }
            header[used] = 0;
            if (!starts_with(header, "HTTP/1.1 200 ") && !starts_with(header, "HTTP/1.0 200 ")) break;
            char *field = strstr(header, "\r\nContent-Length:");
            if (!field) field = strstr(header, "\r\ncontent-length:");
            if (!field) break;
            field = strchr(field + 2, ':') + 1;
            while (*field == ' ') field++;
            if (*field < '0' || *field > '9') break;
            unsigned int value = 0;
            while (*field >= '0' && *field <= '9') {
                if (value > maximum / 10) goto done;
                value = value * 10 + *field++ - '0';
                if (value > maximum) goto done;
            }
            length = value;
        }
        if ((unsigned int)got - offset > length - written) break;
        if (offset < (unsigned int)got) {
            unsigned int count = (unsigned int)got - offset;
            if (sink(buffer + offset, count, context)) break;
            written += count;
        }
        if (written == length) { result = (int)written; break; }
    }
done:
    net_close(handle);
    return result;
}
typedef struct { const char *path; unsigned int offset; } Download;
static int download_chunk(const void *bytes, unsigned int count, void *context)
{
    Download *download = context;
    if (fs_pwrite(download->path, download->offset, bytes, count, 0) < 0) return -1;
    download->offset += count;
    return 0;
}
int net_download(const char *url, const char *destination)
{
    if (!url || !starts_with(url, "http://") || !destination ||
        strlen(destination) > 255 || fs_file_size(destination) >= 0 ||
        fs_write(destination, "", 0)) return -1;
    Download download = {destination, 0};
    int result = net_http_get(url, FS_MAX_FILE_SIZE, download_chunk, &download);
    if (result < 0) fs_rm(destination);
    return result < 0 ? result : 0;
}

/* ------------------------------------------------------------ Ethernet, ARP */
#define ARP_SIZE 16
static struct { unsigned int ip; u8 mac[6]; } arp[ARP_SIZE];
static unsigned int arp_next;
static const u8 broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static int ethernet_send(const u8 *destination, unsigned int type, unsigned int length)
{
    memcpy(frame_out, destination, 6);
    memcpy(frame_out + 6, driver.mac, 6);
    put16(frame_out + 12, type);
    return driver.send(frame_out, 14 + length);
}
static void arp_learn(unsigned int ip, const u8 *mac)
{
    for (int i = 0; i < ARP_SIZE; i++) if (arp[i].ip == ip) { memcpy(arp[i].mac, mac, 6); return; }
    arp[arp_next].ip = ip; memcpy(arp[arp_next].mac, mac, 6);
    arp_next = (arp_next + 1) % ARP_SIZE;
}
static void arp_send(unsigned int operation, const u8 *target_mac, unsigned int target_ip)
{
    u8 *a = frame_out + 14;
    put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, operation);
    memcpy(a + 8, driver.mac, 6); put32(a + 14, my_ip);
    memcpy(a + 18, operation == 1 ? (const u8 *)"\0\0\0\0\0\0" : target_mac, 6); put32(a + 24, target_ip);
    ethernet_send(operation == 1 ? broadcast : target_mac, 0x0806, 28);
}
static int arp_lookup(unsigned int ip, u8 *mac)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        for (int i = 0; i < ARP_SIZE; i++) if (arp[i].ip == ip && ip) { memcpy(mac, arp[i].mac, 6); return 0; }
        arp_send(1, 0, ip);
        for (unsigned int start = net_ms(); net_ms() - start < 700;) {
            net_poll();
            for (int i = 0; i < ARP_SIZE; i++) if (arp[i].ip == ip && ip) { memcpy(mac, arp[i].mac, 6); return 0; }
        }
    }
    return -1;
}

/* ------------------------------------------------------------ IPv4 */
static unsigned int ip_id;
static int ip_frame(unsigned int destination, const u8 *mac, int protocol, const void *payload, unsigned int length)
{
    if (length > 1480) return -1;
    u8 *ip = frame_out + 14;
    ip[0] = 0x45; ip[1] = 0; put16(ip + 2, 20 + length); put16(ip + 4, ++ip_id); put16(ip + 6, 0x4000);
    ip[8] = 64; ip[9] = (u8)protocol; put16(ip + 10, 0); put32(ip + 12, my_ip); put32(ip + 16, destination);
    put16(ip + 10, net_checksum_end(net_checksum_add(0, ip, 20)));
    if (payload != ip + 20) memcpy(ip + 20, payload, length);
    return ethernet_send(mac, 0x0800, 20 + length);
}
int net_ip_send(unsigned int destination, int protocol, const void *payload, unsigned int length)
{
    u8 mac[6];
    if (destination == 0xffffffffU) memcpy(mac, broadcast, 6);
    else {
        unsigned int hop = (destination & my_mask) == (my_ip & my_mask) ? destination : my_gateway;
        if (arp_lookup(hop, mac)) return -1;
    }
    return ip_frame(destination, mac, protocol, payload, length);
}

/* ------------------------------------------------------------ receive */
static struct { unsigned int port, length, source; int ready; u8 data[1500]; } udp_wait;
static struct { unsigned int id, sequence; int done; } ping_wait;
static void icmp_input(unsigned int source, const u8 *src_mac, u8 *icmp, unsigned int length)
{
    if (length < 8) return;
    if (icmp[0] == 8) {   /* answer pings */
        static u8 reply[1480];
        if (length > sizeof(reply)) return;
        memcpy(reply, icmp, length);
        reply[0] = 0; put16(reply + 2, 0);
        put16(reply + 2, net_checksum_end(net_checksum_add(0, reply, length)));
        ip_frame(source, src_mac, 1, reply, length);
    } else if (icmp[0] == 0 && get16(icmp + 4) == ping_wait.id && get16(icmp + 6) == ping_wait.sequence) ping_wait.done = 1;
}
static void ip_input(const u8 *src_mac, u8 *ip, unsigned int length)
{
    if (length < 20 || (ip[0] >> 4) != 4) return;
    unsigned int header = (ip[0] & 15) * 4u, total = get16(ip + 2);
    if (header < 20 || total < header || total > length || net_checksum_end(net_checksum_add(0, ip, header))) return;
    if (get16(ip + 6) & 0x3fff) return;   /* fragments are not reassembled */
    unsigned int source = get32(ip + 12), destination = get32(ip + 16);
    if (destination != my_ip && destination != 0xffffffffU && my_ip) return;
    if (source && (source & my_mask) == (my_ip & my_mask)) arp_learn(source, src_mac);
    u8 *payload = ip + header;
    unsigned int size = total - header;
    if (ip[9] == 1) icmp_input(source, src_mac, payload, size);
    else if (ip[9] == 6) tcp_input(source, payload, size);
    else if (ip[9] == 17 && size >= 8 && get16(payload + 2) == udp_wait.port && !udp_wait.ready) {
        unsigned int data = get16(payload + 4);
        if (data < 8 || data > size) return;
        udp_wait.length = data - 8 > sizeof(udp_wait.data) ? sizeof(udp_wait.data) : data - 8;
        memcpy(udp_wait.data, payload + 8, udp_wait.length);
        udp_wait.source = source; udp_wait.ready = 1;
    }
}
/* Sending may wait for ARP, which polls again: one nested level gets its
 * own buffer; deeper calls return at once. */
void net_poll(void)
{
    static int depth;
    if (!adapter || off || depth > 1) return;
    u8 *frame_in = frames_in[depth++];
    for (int n = 0; n < 256; n++) {
        int length = driver.receive(frame_in, sizeof(frames_in[0]));
        if (length <= 0) break;
        if (length < 14) continue;
        unsigned int type = get16(frame_in + 12);
        if (type == 0x0806 && length >= 42) {
            u8 *a = frame_in + 14;
            unsigned int sender = get32(a + 14), target = get32(a + 24);
            if (sender) arp_learn(sender, a + 8);
            if (get16(a + 6) == 1 && target == my_ip && my_ip) arp_send(2, a + 8, sender);
        } else if (type == 0x0800) ip_input(frame_in + 6, frame_in + 14, (unsigned int)length - 14);
    }
    if (depth == 1) tcp_timers();
    depth--;
}

/* ------------------------------------------------------------ UDP */
static int udp_send(unsigned int destination, unsigned int source_port, unsigned int port, const void *data, unsigned int length)
{
    static u8 packet[1480];
    if (length + 8 > sizeof(packet)) return -1;
    put16(packet, source_port); put16(packet + 2, port); put16(packet + 4, 8 + length); put16(packet + 6, 0);
    memcpy(packet + 8, data, length);
    return net_ip_send(destination, 17, packet, 8 + length);
}
/* Waits for one datagram to port; 0 or -1 after timeout_ms. */
static int udp_receive(unsigned int port, unsigned int timeout_ms)
{
    for (unsigned int start = net_ms(); net_ms() - start < timeout_ms;) {
        net_poll();
        if (udp_wait.ready && udp_wait.port == port) return 0;
    }
    return -1;
}

/* ------------------------------------------------------------ DHCP */
static u8 dhcp_packet[548];
static unsigned int dhcp_build(unsigned int xid, int type, unsigned int requested, unsigned int server)
{
    u8 *p = dhcp_packet;
    memset(p, 0, sizeof(dhcp_packet));
    p[0] = 1; p[1] = 1; p[2] = 6; put32(p + 4, xid); put16(p + 10, 0x8000);
    memcpy(p + 28, driver.mac, 6);
    put32(p + 236, 0x63825363);
    unsigned int n = 240;
    p[n++] = 53; p[n++] = 1; p[n++] = (u8)type;
    p[n++] = 61; p[n++] = 7; p[n++] = 1; memcpy(p + n, driver.mac, 6); n += 6;
    p[n++] = 12; p[n++] = 6; memcpy(p + n, "barnix", 6); n += 6;
    p[n++] = 55; p[n++] = 4; p[n++] = 1; p[n++] = 3; p[n++] = 6; p[n++] = 51;
    if (requested) { p[n++] = 50; p[n++] = 4; put32(p + n, requested); n += 4; }
    if (server) { p[n++] = 54; p[n++] = 4; put32(p + n, server); n += 4; }
    p[n++] = 255;
    return n < 300 ? 300 : n;
}
/* Message type of the answer, its options parsed into the out values. */
static int dhcp_answer(unsigned int xid, unsigned int *offer, unsigned int *server, unsigned int *mask,
                       unsigned int *router, unsigned int *dns)
{
    const u8 *p = udp_wait.data;
    if (udp_wait.length < 240 || p[0] != 2 || get32(p + 4) != xid || get32(p + 236) != 0x63825363) return 0;
    int type = 0;
    *offer = get32(p + 16);
    for (unsigned int i = 240; i + 1 < udp_wait.length;) {
        unsigned int tag = p[i], size = p[i + 1];
        if (tag == 255) break;
        if (tag == 0) { i++; continue; }
        const u8 *v = p + i + 2;
        if (i + 2 + size > udp_wait.length) break;
        if (tag == 53 && size == 1) type = v[0];
        if (tag == 54 && size == 4) *server = get32(v);
        if (tag == 1 && size == 4) *mask = get32(v);
        if (tag == 3 && size >= 4) *router = get32(v);
        if (tag == 6 && size >= 4) *dns = get32(v);
        i += 2 + size;
    }
    return type;
}
/* An address for the link in driver: 0 or BARNIX_NET_NO_ADDRESS. */
static int dhcp(void)
{
    unsigned int xid;
    net_random(&xid, 4);
    my_ip = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        unsigned int offer = 0, server = 0, mask = 0xffffff00U, router = 0, dns = 0, length;
        udp_wait.port = 68; udp_wait.ready = 0;
        length = dhcp_build(xid, 1, 0, 0);
        if (udp_send(0xffffffffU, 68, 67, dhcp_packet, length)) continue;
        int type = 0;
        for (unsigned int begin = net_ms(); net_ms() - begin < 2000 && type != 2;) {
            if (udp_receive(68, 2000 - (net_ms() - begin))) break;
            type = dhcp_answer(xid, &offer, &server, &mask, &router, &dns);
            udp_wait.ready = 0;
        }
        if (type != 2 || !offer) continue;
        length = dhcp_build(xid, 3, offer, server);
        if (udp_send(0xffffffffU, 68, 67, dhcp_packet, length)) continue;
        type = 0;
        for (unsigned int begin = net_ms(); net_ms() - begin < 2000 && type != 5 && type != 6;) {
            if (udp_receive(68, 2000 - (net_ms() - begin))) break;
            type = dhcp_answer(xid, &offer, &server, &mask, &router, &dns);
            udp_wait.ready = 0;
        }
        if (type != 5) continue;
        my_ip = offer; my_mask = mask; my_gateway = router ? router : server; my_dns = dns ? dns : my_gateway;
        udp_wait.port = 0;
        configured = 1;
        return 0;
    }
    udp_wait.port = 0;
    state = NET_DISCONNECTED;
    return BARNIX_NET_NO_ADDRESS;
}
int net_connect_cable(void)
{
    if (!have_cable) return BARNIX_NET_NO_ADAPTER;
    wifi_disconnect();
    unconfigure();
    use_cable();
    off = 0;
    wifi_chosen = 0;
    unsigned int start = net_ms();
    while (!driver.link() && net_ms() - start < 3000) usb_delay_ms(50);   /* auto-negotiation */
    if (!driver.link()) return BARNIX_NET_NO_LINK;
    int result = dhcp();
    if (!result) state = NET_CABLE;
    return result;
}
static int ensure(void)
{
    if (!adapter) return BARNIX_NET_NO_ADAPTER;
    if (off) return BARNIX_NET_OFF;
    if (configured && driver.link()) return 0;
    if (wifi_chosen) return net_connect_wifi(wifi_name, wifi_password);
    return net_connect_cable();
}

/* ------------------------------------------------------------ DNS */
static struct { char name[64]; unsigned int address, expires; } dns_cache[8];
static unsigned int dns_next;
static int parse_dotted(const char *text, unsigned int *address)
{
    unsigned int value = 0, part = 0, dots = 0, digits = 0;
    for (const char *p = text; ; p++) {
        if (*p >= '0' && *p <= '9') { part = part * 10 + (unsigned int)(*p - '0'); digits++; if (part > 255 || digits > 3) return -1; }
        else if (*p == '.' || !*p) {
            if (!digits) return -1;
            value = value << 8 | part; part = 0; digits = 0;
            if (!*p) break;
            if (++dots > 3) return -1;
        } else return -1;
    }
    if (dots != 3) return -1;
    *address = value;
    return 0;
}
static int dns_query(const char *name, unsigned int server, unsigned int *address)
{
    static u8 query[300];
    unsigned int id, n = 12;
    net_random(&id, 2); id &= 0xffff;
    memset(query, 0, 12);
    put16(query, id); query[2] = 1; put16(query + 4, 1);
    for (const char *label = name; *label;) {
        const char *end = label;
        while (*end && *end != '.') end++;
        unsigned int length = (unsigned int)(end - label);
        if (!length || length > 63 || n + length + 6 > sizeof(query)) return -1;
        query[n++] = (u8)length; memcpy(query + n, label, length); n += length;
        label = *end ? end + 1 : end;
    }
    query[n++] = 0; put16(query + n, 1); put16(query + n + 2, 1); n += 4;
    unsigned int port = 50000 + (id & 0x3fff);
    for (int attempt = 0; attempt < 3; attempt++) {
        udp_wait.port = port; udp_wait.ready = 0;
        if (udp_send(server, port, 53, query, n)) return -1;
        for (unsigned int begin = net_ms(); net_ms() - begin < 1500;) {
            if (udp_receive(port, 1500 - (net_ms() - begin))) break;
            const u8 *p = udp_wait.data;
            unsigned int length = udp_wait.length;
            udp_wait.ready = 0;
            if (length < 12 || get16(p) != id || !(p[2] & 0x80)) continue;
            if ((p[3] & 15) == 3) { udp_wait.port = 0; return -1; }   /* no such name */
            unsigned int at = 12, answers = get16(p + 6);
            for (unsigned int q = get16(p + 4); q; q--) {   /* skip the questions */
                while (at < length && p[at] && !(p[at] & 0xc0)) at += p[at] + 1u;
                at += at < length && (p[at] & 0xc0) ? 2 : 1;
                at += 4;
            }
            for (; answers && at + 10 <= length; answers--) {
                while (at < length && p[at] && !(p[at] & 0xc0)) at += p[at] + 1u;
                at += at < length && (p[at] & 0xc0) ? 2 : 1;
                if (at + 10 > length) break;
                unsigned int type = get16(p + at), size = get16(p + at + 8);
                at += 10;
                if (type == 1 && size == 4 && at + 4 <= length) { *address = get32(p + at); udp_wait.port = 0; return 0; }
                at += size;
            }
        }
    }
    udp_wait.port = 0;
    return -1;
}
int net_resolve(const char *host, unsigned int *address)
{
    if (!host || !address) return BARNIX_NET_NO_NAME;
    if (!parse_dotted(host, address)) return 0;
    int problem = ensure();
    if (problem) return problem;
    unsigned int length = (unsigned int)strlen(host);
    if (length >= sizeof(dns_cache[0].name)) length = 0;
    for (int i = 0; i < 8 && length; i++)
        if (!strcmp(dns_cache[i].name, host) && (int)(dns_cache[i].expires - net_ms()) > 0) { *address = dns_cache[i].address; return 0; }
    unsigned int servers[3] = {my_dns, 0x01010101U, 0x08080808U};
    for (int i = 0; i < 3; i++)
        if (servers[i] && !dns_query(host, servers[i], address)) {
            if (length) {
                strcpy(dns_cache[dns_next].name, host);
                dns_cache[dns_next].address = *address;
                dns_cache[dns_next].expires = net_ms() + 300000;
                dns_next = (dns_next + 1) % 8;
            }
            return 0;
        }
    return BARNIX_NET_NO_NAME;
}

/* ------------------------------------------------------------ ICMP echo */
int net_ping_host(unsigned int address, unsigned int *milliseconds)
{
    if (milliseconds) *milliseconds = 0;
    if (ensure()) return -1;
    static u8 echo[40];
    unsigned int id;
    net_random(&id, 2);
    ping_wait.id = id & 0xffff; ping_wait.sequence = (ping_wait.sequence + 1) & 0xffff; ping_wait.done = 0;
    memset(echo, 0, sizeof(echo));
    echo[0] = 8; put16(echo + 4, ping_wait.id); put16(echo + 6, ping_wait.sequence);
    memcpy(echo + 8, "Barnix ping 0123456789abcdef", 28);
    put16(echo + 2, net_checksum_end(net_checksum_add(0, echo, sizeof(echo))));
    unsigned int start = net_ms();
    if (net_ip_send(address, 1, echo, sizeof(echo))) return -1;
    while (net_ms() - start < 3000) {
        net_poll();
        if (ping_wait.done) { if (milliseconds) *milliseconds = net_ms() - start; return 0; }
    }
    return -1;
}
int net_ping(unsigned int *milliseconds)
{
    if (ensure()) { if (milliseconds) *milliseconds = 0; return -1; }
    return net_ping_host(my_gateway, milliseconds);
}

int net_info(BarnixNetInfo *info)
{
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    info->state = net_state();
    if (!adapter) return -1;
    /* The link in use; without one the cable, else the Wi-Fi card */
    if (state == NET_WIFI || !have_cable) {
        const char *name = wifi_adapter();
        info->link = wifi_connected();
        if (state == NET_WIFI) memcpy(info->mac, driver.mac, 6);
        unsigned int n = 0;
        while (name[n] && n + 1 < sizeof(info->adapter)) { info->adapter[n] = name[n]; n++; }
    } else {
        info->link = cable.link();
        memcpy(info->mac, cable.mac, 6);
        strcpy(info->adapter, cable.name);
    }
    if (configured) { info->address = my_ip; info->mask = my_mask; info->gateway = my_gateway; info->dns = my_dns; }
    return 0;
}
