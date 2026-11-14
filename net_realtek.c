/* Realtek Ethernet: RTL8139 (ring buffer receive, four transmit slots) and
 * the RTL8169/8110/8111/8168/8101 family (descriptor rings), through I/O
 * ports, polled. */
#include "net_driver.h"
#include "barnix.h"

static unsigned short io;
static inline void o8(unsigned int r, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"((unsigned short)(io + r))); }
static inline void o16(unsigned int r, unsigned short v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"((unsigned short)(io + r))); }
static inline void o32(unsigned int r, unsigned int v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"((unsigned short)(io + r))); }
static inline unsigned char i8(unsigned int r) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + r))); return v; }
static inline unsigned short i16(unsigned int r) { unsigned short v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + r))); return v; }
static inline unsigned int i32(unsigned int r) { unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + r))); return v; }

static int io_bar(unsigned int pci)
{
    for (unsigned int bar = 0; bar < 6; bar++) {
        unsigned int v = net_pci_read(pci, 0x10 + 4 * bar);
        if ((v & 1) && (v & 0xfffc)) { io = (unsigned short)(v & 0xfffc); return 0; }
    }
    return -1;
}
static int reset(void)
{
    o8(0x37, 0x10);
    for (int i = 0; i < 1000; i++) { if (!(i8(0x37) & 0x10)) return 0; net_delay_ms(1); }
    return -1;
}

/* ------------------------------------------------------------ RTL8139 */
#define RING 32768   /* Wi-Fi bursts through the virtual card need room */
static unsigned char ring[RING + 16 + 2048] __attribute__((aligned(16)));
/* Up to 1792-byte frames (the chip's limit): the virtual Wi-Fi radio
 * (net_vwifi.c) carries 1500-byte packets with 802.11 and CCMP headers. */
#define FRAME8139 1792
static unsigned char slots[4][2048] __attribute__((aligned(16)));
static unsigned int slot, offset;
static void start8139(void)
{
    reset();
    o32(0x30, (unsigned int)ring);
    o16(0x3c, 0);                                                    /* no interrupts */
    o8(0x37, 0x0c);                                                  /* receive, transmit */
    o32(0x44, 0x0e | (1U << 7) | (2U << 11));                        /* match, multicast, broadcast; wrap; 32K ring */
    o32(0x40, 0x03000600);
    offset = 0;
}
static int link8139(void) { return !(i8(0x58) & 4); }
static int send8139(const void *frame, unsigned int length)
{
    if (length > FRAME8139) return -1;
    unsigned int status = 0x10 + 4 * slot;
    for (int i = 0; i < 100000; i++) {   /* the slot is free once the card owns it again */
        unsigned int v = i32(status);
        if ((v & (1U << 13)) || !(v & 0x1fff)) break;
        __asm__ volatile("pause");
    }
    memcpy(slots[slot], frame, length);
    if (length < 60) { memset(slots[slot] + length, 0, 60 - length); length = 60; }
    o32(0x20 + 4 * slot, (unsigned int)slots[slot]);
    o32(status, length);
    slot = (slot + 1) & 3;
    return 0;
}
static int receive8139(void *frame, unsigned int capacity)
{
    if (i8(0x37) & 1) return 0;                         /* buffer empty */
    unsigned short header = *(unsigned short *)(ring + offset), size = *(unsigned short *)(ring + offset + 2);
    int ok = (header & 1) && size >= 64 && size <= FRAME8139 + 4 && (unsigned int)size - 4 <= capacity;
    if (ok) memcpy(frame, ring + offset + 4, size - 4U);
    if (!(header & 1) || size > FRAME8139 + 4) { start8139(); return 0; }
    offset = (offset + size + 4 + 3) & ~3U;
    offset %= RING;
    o16(0x38, (unsigned short)(offset - 16));
    return ok ? size - 4 : 0;
}
int rtl8139_probe(unsigned int pci, unsigned int id, NetDriver *driver)
{
    if (id != 0x813910ec || io_bar(pci)) return -1;
    net_pci_write(pci, 4, (net_pci_read(pci, 4) & 0xffff) | 5);   /* I/O, bus master */
    o8(0x52, 0);                                                     /* power on */
    if (reset()) return -1;
    for (int i = 0; i < 6; i++) driver->mac[i] = i8(i);
    start8139();
    slot = 0;
    strcpy(driver->name, "Realtek RTL8139");
    driver->send = send8139; driver->receive = receive8139; driver->link = link8139;
    return 0;
}

/* ------------------------------------------------------------ RTL8169 family */
#define DESCS 32
typedef struct { volatile unsigned int options, vlan, low, high; } Desc;
static Desc rxd[DESCS] __attribute__((aligned(256)));
static Desc txd[DESCS] __attribute__((aligned(256)));
static unsigned char rxb[DESCS][2048] __attribute__((aligned(16)));
static unsigned char txb[DESCS][2048] __attribute__((aligned(16)));
static unsigned int rx_at, tx_at;
#define OWN (1U << 31)
#define EOR (1U << 30)
static int link8169(void) { return (i8(0x6c) & 2) != 0; }
static int send8169(const void *frame, unsigned int length)
{
    if (length > 1514) return -1;
    Desc *d = &txd[tx_at];
    for (int i = 0; i < 100000 && (d->options & OWN); i++) __asm__ volatile("pause");
    if (d->options & OWN) return -1;
    memcpy(txb[tx_at], frame, length);
    if (length < 60) { memset(txb[tx_at] + length, 0, 60 - length); length = 60; }
    d->low = (unsigned int)txb[tx_at]; d->high = 0; d->vlan = 0;
    __asm__ volatile("" ::: "memory");
    d->options = OWN | (1U << 29) | (1U << 28) | (tx_at == DESCS - 1 ? EOR : 0) | length;
    tx_at = (tx_at + 1) % DESCS;
    o8(0x38, 0x40);                                                  /* poll the normal-priority queue */
    return 0;
}
static int receive8169(void *frame, unsigned int capacity)
{
    Desc *d = &rxd[rx_at];
    unsigned int options = d->options;
    if (options & OWN) return 0;
    unsigned int length = options & 0x3fff;
    int ok = (options & (1U << 29)) && (options & (1U << 28)) && !(options & (1U << 21)) && length >= 4 + 14 && length - 4 <= capacity;
    if (ok) memcpy(frame, rxb[rx_at], length - 4);
    d->vlan = 0;
    __asm__ volatile("" ::: "memory");
    d->options = OWN | (rx_at == DESCS - 1 ? EOR : 0) | 2048;
    rx_at = (rx_at + 1) % DESCS;
    return ok ? (int)length - 4 : 0;
}
int rtl8169_probe(unsigned int pci, unsigned int id, NetDriver *driver)
{
    unsigned int device = id >> 16;
    if ((id & 0xffff) != 0x10ec || (device != 0x8161 && device != 0x8167 && device != 0x8168 &&
                                    device != 0x8169 && device != 0x8136 && device != 0x8162) || io_bar(pci)) return -1;
    net_pci_write(pci, 4, (net_pci_read(pci, 4) & 0xffff) | 5);
    if (reset()) return -1;
    for (int i = 0; i < 6; i++) driver->mac[i] = i8(i);
    for (int i = 0; i < DESCS; i++) {
        rxd[i].low = (unsigned int)rxb[i]; rxd[i].high = 0; rxd[i].vlan = 0;
        rxd[i].options = OWN | (i == DESCS - 1 ? EOR : 0) | 2048;
        txd[i].low = txd[i].high = txd[i].vlan = 0; txd[i].options = i == DESCS - 1 ? EOR : 0;
    }
    rx_at = tx_at = 0;
    o8(0x50, 0xc0);                                                  /* unlock configuration */
    o16(0x3c, 0); o16(0x3e, 0xffff);                                 /* no interrupts */
    o16(0xe0, i16(0xe0) & ~(1U << 5));                               /* C+: no checksum offload */
    o16(0xda, 2048);                                                 /* largest received frame */
    o8(0xec, 0x3b);                                                  /* largest transmitted */
    o32(0x20, (unsigned int)txd); o32(0x24, 0);
    o32(0xe4, (unsigned int)rxd); o32(0xe8, 0);
    o8(0x37, 0x0c);                                                  /* receive, transmit */
    o32(0x40, (3U << 24) | (7U << 8));                               /* standard gap, unlimited DMA */
    o32(0x44, (7U << 13) | (7U << 8) | 0x0e);                        /* match, multicast, broadcast */
    o32(0x08, 0xffffffffU); o32(0x0c, 0xffffffffU);                  /* multicast filter open */
    o8(0x50, 0x00);
    strcpy(driver->name, device == 0x8136 ? "Realtek RTL8101/8102" : device == 0x8169 ? "Realtek RTL8169" : "Realtek RTL8111/8168");
    driver->send = send8169; driver->receive = receive8169; driver->link = link8169;
    return 0;
}
