#ifndef BARNIX_NET_H
#define BARNIX_NET_H
#include "netinfo.h"
enum { NET_DISCONNECTED = 0, NET_WIFI = 1, NET_CABLE = 2 };
void net_init(void);
/* Joins a Wi-Fi network and asks it for an address (DHCP): 0 or a
 * BARNIX_NET_* error. name 0 turns all networking off (bnm off) until a
 * program connects again with net_connect_cable or a name. */
int net_connect_wifi(const char *name, const char *password);
/* DHCP on the wired adapter: 0, or a BARNIX_NET_* error. */
int net_connect_cable(void);
int net_scan(char *out, unsigned int size);
int net_ping(unsigned int *milliseconds);   /* the gateway */
int net_download(const char *url, const char *destination);
typedef int (*NetBodySink)(const void *bytes, unsigned int length, void *context);
/* HTTP/1.0 or 1.1 with Content-Length, streamed without retaining the body. */
int net_http_get(const char *url, unsigned int maximum, NetBodySink sink, void *context);
int net_state(void);

/* ABI 12: TCP connections for programs (BDK barnix_net.h adds HTTP/TLS).
 * net_open connects by name or dotted address, configuring the network by
 * DHCP first when needed: a handle, or a BARNIX_NET_* error. net_recv
 * returns bytes, 0 when the peer closed, -2 on timeout, -1 on error. */
int net_open(const char *host, unsigned int port);
int net_send(int handle, const void *data, unsigned int length);
int net_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms);
void net_close(int handle);
int net_resolve(const char *host, unsigned int *address);
int net_info(BarnixNetInfo *info);
int net_ping_host(unsigned int address, unsigned int *milliseconds);
void net_random(void *out, unsigned int length);
void net_reset(void);   /* program exit: drop its connections */

/* Shared by net.c and net_tcp.c */
unsigned int net_ms(void);
void net_poll(void);
unsigned int net_address(void);
int net_ip_send(unsigned int destination, int protocol, const void *payload, unsigned int length);
unsigned int net_checksum_add(unsigned int sum, const void *data, unsigned int length);
unsigned short net_checksum_end(unsigned int sum);
void tcp_input(unsigned int source, const unsigned char *segment, unsigned int length);
void tcp_timers(void);
#endif
