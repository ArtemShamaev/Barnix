/* Runs the Barnix Wi-Fi station (wifi.c) and the virtual card driver
 * (net_vwifi.c) on the host, with the card's Ethernet carried over UDP the
 * way QEMU's dgram netdev does, against scripts/virtual_wifi.py
 * (tests/wifi_test.py drives it).
 *
 *   wifi_test LOCAL_PORT AP_PORT SSID PASSWORD
 *
 * Prints "scan N", the scan lines, "connect R"; when connected it sends a
 * broadcast Ethernet frame with "hello from barnix" and prints
 * "received TYPE TEXT" for the first frame that comes back. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "wifi.h"
#include "net.h"

/* net_vwifi.c is compiled with these names for the kernel's string calls */
void *vw_memcpy(void *d, const void *s, int n) { return memcpy(d, s, (size_t)n); }
void *vw_memset(void *d, int v, int n) { return memset(d, v, (size_t)n); }
int vw_memcmp(const void *a, const void *b, int n) { return memcmp(a, b, (size_t)n); }
void vw_strcpy(char *d, const char *s) { strcpy(d, s); }

unsigned int net_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
void net_random(void *out, unsigned int length) { unsigned char *p = out; while (length--) *p++ = (unsigned char)rand(); }

static int sock;
static struct sockaddr_in ap;
static int udp_send(const void *frame, unsigned int length)
{
    return sendto(sock, frame, length, 0, (struct sockaddr *)&ap, sizeof(ap)) == (ssize_t)length ? 0 : -1;
}
static int udp_receive(void *frame, unsigned int capacity)
{
    ssize_t n = recv(sock, frame, capacity, MSG_DONTWAIT);
    if (n <= 0) { usleep(200); return 0; }
    return (int)n;
}
static int udp_link(void) { return 1; }

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: wifi_test LOCAL_PORT AP_PORT SSID PASSWORD\n"); return 2; }
    srand((unsigned int)time(0) ^ (unsigned int)getpid());
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET; local.sin_port = htons((unsigned short)atoi(argv[1])); local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, (struct sockaddr *)&local, sizeof(local))) { perror("bind"); return 2; }
    ap.sin_family = AF_INET; ap.sin_port = htons((unsigned short)atoi(argv[2])); ap.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    NetDriver ethernet = {"udp", {0x02, 0x57, 0x49, 0x46, 0x49, 0x01}, udp_send, udp_receive, udp_link};
    if (vwifi_probe(&ethernet)) { puts("probe failed"); return 1; }
    printf("adapter %s\n", wifi_adapter());

    static WifiBss list[8];
    int count = wifi_scan(list, 8);
    printf("scan %d\n", count);
    for (int i = 0; i < count; i++)
        printf("network %s %s %d\n", list[i].ssid, list[i].security == WIFI_WPA2 ? "WPA2" : list[i].security == WIFI_OPEN ? "open" : list[i].why, list[i].rssi);

    NetDriver link;
    int result = wifi_connect(argv[3], argv[4], &link);
    printf("connect %d\n", result);
    fflush(stdout);
    if (result) return 0;

    unsigned char frame[1600];
    memset(frame, 0xff, 6); memcpy(frame + 6, link.mac, 6); frame[12] = 0x08; frame[13] = 0x00;
    strcpy((char *)frame + 14, "hello from barnix");
    if (link.send(frame, 14 + 18)) { puts("send failed"); return 1; }
    for (unsigned int start = net_ms(); net_ms() - start < 3000;) {
        int n = link.receive(frame, sizeof(frame));
        if (n > 14) {
            frame[n < 1599 ? n : 1599] = 0;
            printf("received %02x%02x %s\n", frame[12], frame[13], frame + 14);
            printf("link %d\n", link.link());
            fflush(stdout);
            wifi_disconnect();
            return 0;
        }
    }
    puts("received nothing");
    return 1;
}
