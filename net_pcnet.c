/* AMD PCnet-PCI (Am79C970A and relatives, QEMU and VirtualBox): 32-bit
 * I/O, software style 2 descriptors, polled. */
#include "net_driver.h"
#include "barnix.h"

#define COUNT 16
typedef struct { volatile unsigned int address, flags, misc, reserved; } Desc;
static Desc rx[COUNT] __attribute__((aligned(16)));
static Desc tx[COUNT] __attribute__((aligned(16)));
static unsigned char rxb[COUNT][1544] __attribute__((aligned(16)));
static unsigned char txb[COUNT][1544] __attribute__((aligned(16)));
static struct { unsigned short mode; unsigned char rlen, tlen; unsigned char mac[6]; unsigned short reserved;
                unsigned int filter[2], rx, tx; } __attribute__((packed, aligned(4))) init;
static unsigned short io;
static unsigned int rx_at, tx_at;
static void out32(unsigned int r, unsigned int v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"((unsigned short)(io + r))); }
static unsigned int in32(unsigned int r) { unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + r))); return v; }
static void csr(unsigned int n, unsigned int v) { out32(0x14, n); out32(0x10, v); }
static unsigned int csr_read(unsigned int n) { out32(0x14, n); return in32(0x10); }
static void bcr(unsigned int n, unsigned int v) { out32(0x14, n); out32(0x1c, v); }
static int link(void) { return 1; }
static int send(const void *frame, unsigned int length)
{
    if (length > 1514) return -1;
    Desc *d = &tx[tx_at];
    for (int i = 0; i < 100000 && (d->flags & (1U << 31)); i++) __asm__ volatile("pause");
    if (d->flags & (1U << 31)) return -1;
    memcpy(txb[tx_at], frame, length);
    if (length < 60) { memset(txb[tx_at] + length, 0, 60 - length); length = 60; }
    d->address = (unsigned int)txb[tx_at]; d->misc = 0;
    __asm__ volatile("" ::: "memory");
    d->flags = (1U << 31) | (1U << 25) | (1U << 24) | 0xf000 | ((-(int)length) & 0xfff);
    tx_at = (tx_at + 1) % COUNT;
    csr(0, 0x0008);                                                  /* transmit demand */
    return 0;
}
static int receive(void *frame, unsigned int capacity)
{
    Desc *d = &rx[rx_at];
    unsigned int flags = d->flags;
    if (flags & (1U << 31)) {
        /* The card may start at another slot after initialization: follow it
         * to the next filled descriptor. */
        for (unsigned int k = 1; k < COUNT; k++)
            if (!(rx[(rx_at + k) % COUNT].flags & (1U << 31))) { rx_at = (rx_at + k) % COUNT; break; }
        d = &rx[rx_at];
        flags = d->flags;
        if (flags & (1U << 31)) return 0;
    }
    unsigned int length = d->misc & 0xfff;
    /* The count includes the FCS on real cards but not on every emulation:
     * keep it, the IP layer knows its own length. */
    if (length > capacity) length = capacity;
    int ok = !(flags & (1U << 30)) && (flags & (1U << 25)) && (flags & (1U << 24)) && length >= 14;
    if (ok) memcpy(frame, rxb[rx_at], length);
    d->misc = 0;
    __asm__ volatile("" ::: "memory");
    d->flags = (1U << 31) | 0xf000 | ((-(int)sizeof(rxb[0])) & 0xfff);
    rx_at = (rx_at + 1) % COUNT;
    return ok ? (int)length : 0;
}
int pcnet_probe(unsigned int pci, unsigned int id, NetDriver *driver)
{
    if (id != 0x20001022) return -1;
    unsigned int bar = net_pci_read(pci, 0x10);
    if (!(bar & 1)) return -1;
    io = (unsigned short)(bar & 0xfffc);
    net_pci_write(pci, 4, (net_pci_read(pci, 4) & 0xffff) | 5);
    (void)in32(0x18);                                                /* reset (32-bit) */
    { unsigned short v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + 0x14))); }   /* reset (16-bit) */
    net_delay_ms(2);
    for (int i = 0; i < 6; i++) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"((unsigned short)(io + i))); driver->mac[i] = v; }
    out32(0x10, 0);                                                  /* switch to 32-bit I/O */
    bcr(20, 2);                                                      /* software style 2 */
    for (int i = 0; i < COUNT; i++) {
        rx[i].address = (unsigned int)rxb[i]; rx[i].misc = 0;
        rx[i].flags = (1U << 31) | 0xf000 | ((-(int)sizeof(rxb[0])) & 0xfff);
        tx[i].address = 0; tx[i].flags = 0; tx[i].misc = 0;
    }
    rx_at = tx_at = 0;
    memset(&init, 0, sizeof(init));
    init.rlen = 4 << 4; init.tlen = 4 << 4;                          /* 16 entries each */
    memcpy(init.mac, driver->mac, 6);
    init.rx = (unsigned int)rx; init.tx = (unsigned int)tx;
    csr(1, (unsigned int)&init & 0xffff); csr(2, (unsigned int)&init >> 16);
    csr(4, csr_read(4) | 0x0800);                                    /* pad short frames */
    csr(0, 0x0001);                                                  /* initialize */
    for (int i = 0; i < 100 && !(csr_read(0) & 0x0100); i++) net_delay_ms(1);
    if (!(csr_read(0) & 0x0100)) return -1;
    csr(0, 0x0100 | 0x0002);                                         /* start */
    strcpy(driver->name, "AMD PCnet");
    driver->send = send; driver->receive = receive; driver->link = link;
    return 0;
}
