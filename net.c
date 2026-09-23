#include "net.h"

/* PCI discovery and the reset paths for the two adapters supported by
 * Barnix.  Packet queues and TCP are layered above this small driver API. */
static int state, adapter;
static unsigned short io_base;
static unsigned int mmio_base;
int e1000_reset(unsigned int mmio);
int e1000_send(const void *packet, unsigned int length);
int e1000_receive(void *packet, unsigned int capacity);
int e1000_mac(unsigned char out[6]);
int e1000_link_up(void);
int net_ip_ping(unsigned int *milliseconds);
int net_ip_dhcp(void);
int pcnet_reset(unsigned short io);

static inline void outl(unsigned short port, unsigned int value)
{ __asm__ volatile("outl %0,%1" : : "a"(value), "Nd"(port)); }
static inline unsigned int inl(unsigned short port)
{ unsigned int value; __asm__ volatile("inl %1,%0" : "=a"(value) : "Nd"(port)); return value; }
static unsigned int pci_read(unsigned int bus, unsigned int slot, unsigned int fn, unsigned int reg)
{
    outl(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & 0xFC));
    return inl(0xCFC);
}
static void pci_write(unsigned int bus, unsigned int slot, unsigned int fn, unsigned int reg, unsigned int value)
{
    outl(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & 0xFC));
    outl(0xCFC, value);
}
void net_init(void)
{
    state = NET_DISCONNECTED; adapter = 0; io_base = 0; mmio_base = 0;
    for (unsigned int bus = 0; bus < 256 && !adapter; bus++)
        for (unsigned int slot = 0; slot < 32 && !adapter; slot++) {
            unsigned int id = pci_read(bus, slot, 0, 0);
            unsigned int vendor = id & 0xFFFF, device = id >> 16;
            if (vendor == 0xFFFF) continue;
            int kind = (vendor == 0x8086 &&
                        (device == 0x100E || device == 0x100F || device == 0x10D3 || device == 0x10EA)) ? 1 :
                       (vendor == 0x1022 && (device == 0x2000 || device == 0x2001)) ? 2 : 0;
            if (!kind) continue;
            unsigned int command = pci_read(bus, slot, 0, 4) | 0x7;
            pci_write(bus, slot, 0, 4, command);
            unsigned int bar = pci_read(bus, slot, 0, 0x10);
            if (kind == 1) mmio_base = bar & ~0xFU;
            else io_base = (unsigned short)(bar & ~3U);
            adapter = kind;
            if (kind == 1) e1000_reset(mmio_base); else pcnet_reset(io_base);
        }
}
int net_state(void) { return adapter ? state : -1; }
int net_connect_wifi(const char *name, const char *password)
{ (void)name; (void)password; return -2; }
int net_connect_cable(void)
{
    if (!adapter) return -2;
    if (adapter == 1 && !e1000_link_up()) return -3;
    state = NET_CABLE; net_ip_dhcp(); return 0;
}
int net_scan(char *out, unsigned int size)
{ if (out && size) out[0] = 0; return -2; }
int net_ping(unsigned int *milliseconds)
{ if (milliseconds) *milliseconds = 0; return state == NET_DISCONNECTED ? -1 : net_ip_ping(milliseconds); }
int net_download(const char *url, const char *destination)
{ (void)url; (void)destination; return state == NET_DISCONNECTED ? -1 : -2; }
int net_packet_send(const void *packet, unsigned int length)
{ return adapter == 1 && state != NET_DISCONNECTED ? e1000_send(packet, length) : -1; }
int net_packet_receive(void *packet, unsigned int capacity)
{ return adapter == 1 && state != NET_DISCONNECTED ? e1000_receive(packet, capacity) : -1; }
int net_mac(unsigned char out[6])
{ return adapter == 1 ? e1000_mac(out) : -1; }
