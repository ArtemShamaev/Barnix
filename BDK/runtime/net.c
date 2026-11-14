/* Barnix Net on Barnix: TCP through the ABI 12 kernel calls, the trust
 * store from /etc/ssl/roots, randomness from the kernel stirred with
 * SHA-256, and the date from the CMOS clock. */
#include <barnix_api.h>
#include <barnix_net.h>
#include "bnet.h"

static int available(void) { return barnix && barnix->version >= 12; }

const char *bnet_error(int code)
{
    switch (code) {
    case BARNIX_NET_NO_ADAPTER: return "no supported network adapter";
    case BARNIX_NET_NO_LINK: return "the network cable is not connected";
    case BARNIX_NET_NO_ADDRESS: return "no address from DHCP (is there a router?)";
    case BARNIX_NET_NO_NAME: return "cannot find that host name (DNS)";
    case BARNIX_NET_REFUSED: return "the server refused the connection";
    case BARNIX_NET_TIMEOUT: return "the server did not answer";
    case BARNIX_NET_BUSY: return "too many open connections";
    case BARNIX_NET_OFF: return "networking is turned off (bnm internet or bnm connect)";
    default: return "network error";
    }
}
int bnet_tcp_open(const char *host, unsigned int port)
{
    if (!available()) return BARNIX_NET_NO_ADAPTER;
    return barnix->net_open(host, port);
}
int bnet_tcp_send(int handle, const void *data, unsigned int length) { return available() ? barnix->net_send(handle, data, length) : -1; }
int bnet_tcp_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms)
{ return available() ? barnix->net_recv(handle, data, capacity, timeout_ms) : -1; }
void bnet_tcp_close(int handle) { if (available()) barnix->net_close(handle); }

static int (*idle_hook)(void);
static int stopped;
void bnet_set_idle(int (*idle)(void)) { idle_hook = idle; }
static void put_error(char *error, unsigned int capacity, const char *text)
{
    unsigned int n = 0;
    while (text[n] && n + 1 < capacity) { error[n] = text[n]; n++; }
    error[n] = 0;
}
int bnet_platform_open(const char *host, unsigned int port, char *error, unsigned int capacity)
{
    stopped = 0;
    if (idle_hook && available()) {
        /* Connect without waiting in the kernel: port | 0x10000 returns at
         * once; net_recv with no buffer reports 1 when connected. */
        int handle = barnix->net_open(host, port | 0x10000);
        if (handle < 0) { put_error(error, capacity, bnet_error(handle)); return -1; }
        for (unsigned int waited = 0;; waited += 30) {
            int state = barnix->net_recv(handle, 0, 0, 30);
            if (state == 1) return handle;
            if (state == -1 || waited >= 20000) { barnix->net_close(handle); put_error(error, capacity, bnet_error(state == -1 ? -5 : -6)); return -1; }
            if (idle_hook()) { stopped = 1; barnix->net_close(handle); put_error(error, capacity, "stopped"); return -1; }
        }
    }
    if (!available()) {
        const char *text = "this Barnix kernel has no network support (ABI 12)";
        unsigned int n = 0;
        while (text[n] && n + 1 < capacity) { error[n] = text[n]; n++; }
        error[n] = 0;
        return -1;
    }
    int handle = barnix->net_open(host, port);
    if (handle < 0) {
        const char *text = bnet_error(handle);
        unsigned int n = 0;
        while (text[n] && n + 1 < capacity) { error[n] = text[n]; n++; }
        error[n] = 0;
        return -1;
    }
    return handle;
}
/* Every request starts with a send, also on a kept connection that
 * bnet_platform_open did not reset. */
int bnet_platform_send(int handle, const void *data, unsigned int length) { stopped = 0; return bnet_tcp_send(handle, data, length); }
int bnet_platform_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms)
{
    if (!idle_hook || !available() || !capacity) return bnet_tcp_recv(handle, data, capacity, timeout_ms);
    if (stopped) return -1;
    for (unsigned int waited = 0;;) {
        unsigned int slice = timeout_ms - waited < 30 ? timeout_ms - waited : 30;
        int got = barnix->net_recv(handle, data, capacity, slice);
        if (got != -2) return got;
        waited += slice;
        if (waited >= timeout_ms) return -2;
        if (idle_hook()) { stopped = 1; return -1; }
    }
}
void bnet_platform_close(int handle) { bnet_tcp_close(handle); }

void bnet_platform_random(void *out, unsigned int length)
{
    static uint8_t pool[32];
    static unsigned int counter;
    uint8_t seed[64 + 32 + 8], block[32];
    if (available()) barnix->random(seed, 64);
    unsigned int lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    memcpy(seed + 64, pool, 32);
    memcpy(seed + 96, &lo, 4); memcpy(seed + 100, &hi, 4);
    hash_digest(256, seed, sizeof(seed), pool);
    uint8_t *p = out;
    for (unsigned int done = 0; done < length; done += 32) {
        uint8_t input[36];
        memcpy(input, pool, 32);
        counter++;
        memcpy(input + 32, &counter, 4);
        hash_digest(256, input, 36, block);
        for (unsigned int i = 0; i < 32 && done + i < length; i++) p[done + i] = block[i];
    }
    hash_digest(256, pool, 32, pool);   /* forget what was handed out */
}

const uint8_t *bnet_platform_roots(size_t *length)
{
    static uint8_t roots[131072];
    static int size = -1;
    if (size < 0 && barnix) {
        int n = barnix->size("/etc/ssl/roots");
        if (n > 0 && n <= (int)sizeof(roots) && barnix->read("/etc/ssl/roots", 0, roots, (unsigned int)n) == n) size = n;
    }
    if (size <= 0) return 0;
    *length = (size_t)size;
    return roots;
}

static unsigned char cmos(unsigned char reg)
{
    unsigned char v;
    __asm__ volatile("outb %0,%1" :: "a"(reg), "Nd"((unsigned short)0x70));
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"((unsigned short)0x71));
    return v;
}
/* The CMOS clock, taken as UTC (certificates are valid for months, so a
 * time zone offset does not matter). */
int bnet_platform_now(char out[15])
{
    unsigned char a[6], b[6];
    for (int tries = 0; tries < 5; tries++) {
        for (int i = 0; i < 1000 && (cmos(0x0a) & 0x80); i++) {}
        a[0] = cmos(0); a[1] = cmos(2); a[2] = cmos(4); a[3] = cmos(7); a[4] = cmos(8); a[5] = cmos(9);
        for (int i = 0; i < 1000 && (cmos(0x0a) & 0x80); i++) {}
        b[0] = cmos(0); b[1] = cmos(2); b[2] = cmos(4); b[3] = cmos(7); b[4] = cmos(8); b[5] = cmos(9);
        if (!memcmp(a, b, 6)) break;
    }
    unsigned char status = cmos(0x0b);
    int v[6];
    for (int i = 0; i < 6; i++) {
        int raw = i == 2 ? b[i] & 0x7f : b[i];
        v[i] = status & 4 ? raw : (raw >> 4) * 10 + (raw & 15);
    }
    if (!(status & 2) && (b[2] & 0x80)) v[2] = v[2] % 12 + 12;
    else if (!(status & 2) && v[2] == 12) v[2] = 0;
    if (v[4] < 1 || v[4] > 12 || v[3] < 1 || v[3] > 31 || v[5] < 20) return -1;   /* no believable date */
    int fields[6] = {2000 + v[5], v[4], v[3], v[2], v[1], v[0]};
    char *p = out;
    for (int i = 0; i < 6; i++) {
        int digits = i ? 2 : 4, value = fields[i];
        for (int k = digits - 1; k >= 0; k--) { p[k] = (char)('0' + value % 10); value /= 10; }
        p += digits;
    }
    *p = 0;
    return 0;
}
