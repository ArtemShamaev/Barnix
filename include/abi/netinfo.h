#ifndef BARNIX_NETINFO_H
#define BARNIX_NETINFO_H
/* ABI 12 network state for programs (addresses in host byte order). */
typedef struct {
    int state;                 /* -1 no adapter, 0 not connected, 1 Wi-Fi, 2 cable */
    int link;                  /* cable plugged in, or Wi-Fi associated */
    unsigned char mac[6];
    unsigned int address, mask, gateway, dns;
    char adapter[48];
} BarnixNetInfo;
/* net_open errors */
#define BARNIX_NET_NO_ADAPTER (-1)
#define BARNIX_NET_NO_LINK (-2)
#define BARNIX_NET_NO_ADDRESS (-3)
#define BARNIX_NET_NO_NAME (-4)
#define BARNIX_NET_REFUSED (-5)
#define BARNIX_NET_TIMEOUT (-6)
#define BARNIX_NET_BUSY (-7)
/* Wi-Fi (net_connect_wifi, net_scan) */
#define BARNIX_NET_NO_DRIVER (-8)      /* a Wi-Fi card Barnix cannot drive yet */
#define BARNIX_NET_NOT_FOUND (-9)      /* no network with that name */
#define BARNIX_NET_WRONG_KEY (-10)     /* the handshake failed: wrong password */
#define BARNIX_NET_REJECTED (-11)      /* the access point refused us */
#define BARNIX_NET_UNSUPPORTED (-12)   /* security Barnix does not speak (WPA3, WEP, Enterprise) */
#define BARNIX_NET_OFF (-13)           /* networking was turned off (bnm off) */
#endif
