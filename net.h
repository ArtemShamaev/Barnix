#ifndef BARNIX_NET_H
#define BARNIX_NET_H
enum { NET_DISCONNECTED = 0, NET_WIFI = 1, NET_CABLE = 2 };
void net_init(void);
int net_connect_wifi(const char *name, const char *password);
int net_connect_cable(void);
int net_scan(char *out, unsigned int size);
int net_ping(unsigned int *milliseconds);
int net_download(const char *url, const char *destination);
int net_state(void);
int net_packet_send(const void *packet, unsigned int length);
int net_packet_receive(void *packet, unsigned int capacity);
int net_mac(unsigned char out[6]);
int net_ip_dhcp(void);
int net_dns_resolve(const char *name, unsigned int *address);
#endif
