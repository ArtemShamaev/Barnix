/* Ethernet adapter drivers: polled, no interrupts. Each probe takes the PCI
 * address (0x80000000 | bus << 16 | slot << 11 | fn << 8) and fills the
 * driver when it can run the card. */
#ifndef BARNIX_NET_DRIVER_H
#define BARNIX_NET_DRIVER_H
typedef struct {
    char name[48];
    unsigned char mac[6];
    int (*send)(const void *frame, unsigned int length);       /* 0 or -1 */
    int (*receive)(void *frame, unsigned int capacity);        /* bytes, 0 when none */
    int (*link)(void);
} NetDriver;
int e1000_probe(unsigned int pci, unsigned int id, NetDriver *driver);
int rtl8139_probe(unsigned int pci, unsigned int id, NetDriver *driver);
int rtl8169_probe(unsigned int pci, unsigned int id, NetDriver *driver);
int pcnet_probe(unsigned int pci, unsigned int id, NetDriver *driver);
/* QEMU's paravirtual Wi-Fi radio on top of an adapter (net_vwifi.c): 0 when
 * the adapter is one, and then it belongs to the Wi-Fi layer. */
int vwifi_probe(const NetDriver *ethernet);
/* PCI helpers shared by the drivers */
unsigned int net_pci_read(unsigned int pci, unsigned int reg);
void net_pci_write(unsigned int pci, unsigned int reg, unsigned int value);
void net_delay_ms(unsigned int ms);
#endif
