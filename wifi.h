/* Barnix Wi-Fi: one 802.11 station layer for every card. A card driver
 * (WifiHw) only finds networks, tunes to a channel and moves raw 802.11
 * frames; this layer joins the network (open system authentication,
 * association), runs the WPA2-Personal 4-way handshake, encrypts with CCMP
 * and hands Ethernet frames to the TCP/IP stack as a NetDriver. */
#ifndef BARNIX_WIFI_H
#define BARNIX_WIFI_H
#include "net_driver.h"

enum { WIFI_OPEN, WIFI_WPA2, WIFI_UNSUPPORTED };
typedef struct {
    unsigned char bssid[6];
    char ssid[33];
    unsigned char ssid_length, channel, security;
    signed char rssi;                   /* dBm */
    unsigned short capability;
    unsigned char rates[16], rates_length;   /* Supported Rates element of the AP */
    unsigned char ext_rates[16], ext_rates_length;
    char why[24];                       /* for WIFI_UNSUPPORTED: "WPA3", "WEP", ... */
} WifiBss;

/* A card driver. All calls are polled and must return within ~100 ms. */
typedef struct {
    char name[64];                      /* "Intel Wi-Fi 6 AX201" */
    unsigned char mac[6];
    /* Starts a scan over all channels; beacons and probe responses then
     * arrive through rx. scanning() is 1 until it is finished. */
    int (*scan)(void);
    int (*scanning)(void);
    /* Before authentication: tune to the AP and let frames from it in. */
    int (*join)(const WifiBss *bss);
    /* After association (aid from the AP) and after the keys are known. */
    int (*associated)(const WifiBss *bss, unsigned int aid);
    void (*leave)(void);
    int (*tx)(const void *frame, unsigned int length);              /* 802.11 frame, no FCS */
    int (*rx)(void *frame, unsigned int capacity, int *rssi);        /* bytes, 0 when none */
} WifiHw;

/* Card probes: 0 when the driver runs the card (fills hw). */
typedef int (*WifiProbe)(unsigned int pci, unsigned int id, WifiHw *hw, char *status, unsigned int status_size);

/* net.c calls wifi_identify for every PCI network controller (class 0x0280):
 * 1 when it is a Wi-Fi card Barnix knows (driven or not). */
int wifi_identify(unsigned int pci, unsigned int id);
const char *wifi_adapter(void);         /* card name, or 0 */
const char *wifi_status(void);          /* what the driver is doing or why it cannot */
int wifi_ready(void);                   /* a driver runs the card */
int wifi_scan(WifiBss *out, int max);   /* count, or a negative BARNIX_NET_* error */
/* Joins ssid (password ignored on open networks); fills driver with the
 * Ethernet face of the link. 0 or a negative BARNIX_NET_* error. */
int wifi_connect(const char *ssid, const char *password, NetDriver *driver);
int wifi_connected(void);
const char *wifi_ssid(void);
void wifi_disconnect(void);
/* The card itself, for tests and drivers that start late. */
void wifi_attach(const WifiHw *hw);

/* Platform: milliseconds (net.c). */
unsigned int net_ms(void);
#endif
