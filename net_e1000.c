/* Intel PRO/1000 (8254x, 82574 and the 8257x-I219 PCH family) with legacy
 * descriptors: one receive and one transmit ring, polled. */
#include "net_driver.h"
#include "barnix.h"
#include "linux_memory.h"
#include "storage_pci.h"

#define RX_COUNT 256
#define TX_COUNT 64
typedef struct { unsigned int address, high; unsigned short length, checksum; unsigned char status, errors; unsigned short special; } Rx;
typedef struct { unsigned int address, high; unsigned short length; unsigned char cso, command, status, css; unsigned short special; } Tx;
static Rx rx[RX_COUNT] __attribute__((aligned(128)));
static Tx tx[TX_COUNT] __attribute__((aligned(128)));
static unsigned char rx_buffer[RX_COUNT][2048] __attribute__((aligned(16)));
static unsigned char tx_buffer[TX_COUNT][2048] __attribute__((aligned(16)));
static volatile unsigned char *base;
static unsigned int rx_next, tx_next;

static unsigned int r32(unsigned int reg) { return *(volatile unsigned int *)(base + reg); }
static void w32(unsigned int reg, unsigned int value) { *(volatile unsigned int *)(base + reg) = value; }

static int link(void) { return (r32(0x08) & 2) != 0; }
static int send(const void *frame, unsigned int length)
{
    if (length > 1514) return -1;
    Tx *d = &tx[tx_next];
    for (int i = 0; i < 100000 && !(d->status & 1); i++) __asm__ volatile("pause");
    if (!(d->status & 1)) return -1;
    memcpy(tx_buffer[tx_next], frame, length);
    if (length < 60) { memset(tx_buffer[tx_next] + length, 0, 60 - length); length = 60; }
    d->address = (unsigned int)tx_buffer[tx_next]; d->high = 0;
    d->length = (unsigned short)length; d->cso = 0; d->css = 0; d->special = 0;
    d->command = 0x01 | 0x02 | 0x08;   /* end of packet, insert FCS, report status */
    d->status = 0;
    __asm__ volatile("" ::: "memory");
    tx_next = (tx_next + 1) % TX_COUNT;
    w32(0x3818, tx_next);
    return 0;
}
static int receive(void *frame, unsigned int capacity)
{
    Rx *d = &rx[rx_next];
    if (!(d->status & 1)) return 0;
    unsigned int length = d->length;
    int ok = (d->status & 2) && !d->errors && length <= capacity;
    if (ok) memcpy(frame, rx_buffer[rx_next], length);
    d->status = 0;
    __asm__ volatile("" ::: "memory");
    w32(0x2818, rx_next);
    rx_next = (rx_next + 1) % RX_COUNT;
    return ok ? (int)length : 0;
}
static unsigned int eeprom(unsigned int word)
{
    for (int variant = 0; variant < 2; variant++) {   /* 8254x: done bit 4; newer: bit 1 */
        unsigned int shift = variant ? 2 : 8, done = variant ? 2 : 16;
        w32(0x14, 1 | word << shift);
        for (int i = 0; i < 10000; i++) {
            unsigned int v = r32(0x14);
            if (v & done) return v >> 16;
        }
    }
    return 0xffff;
}
int e1000_probe(unsigned int pci, unsigned int id, NetDriver *driver)
{
    unsigned int device = id >> 16;
    static const unsigned short known[] = {
        0x100e, 0x100f, 0x1004, 0x1008, 0x1009, 0x100c, 0x100d, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016,
        0x1017, 0x1018, 0x1019, 0x101a, 0x101d, 0x101e, 0x1026, 0x1027, 0x1028, 0x1075, 0x1076, 0x1077, 0x1078,
        0x1079, 0x107a, 0x107b, 0x107c, 0x108a, 0x1099, 0x10b5, 0x10b9, 0x105e, 0x105f, 0x1060, 0x107d, 0x107e,
        0x107f, 0x108b, 0x108c, 0x109a, 0x10a4, 0x10a5, 0x10bc, 0x10c4, 0x10c5, 0x10d3, 0x10d5, 0x10d9, 0x10da,
        0x10f6, 0x150c, 0x10bd, 0x294c, 0x10e5, 0x10ea, 0x10eb, 0x10ef, 0x10f0, 0x1502, 0x1503, 0x153a, 0x153b,
        0x155a, 0x1559, 0x15a0, 0x15a1, 0x15a2, 0x15a3, 0x156f, 0x1570, 0x15b7, 0x15b8, 0x15b9, 0x15bb, 0x15bc,
        0x15bd, 0x15be, 0x15d6, 0x15d7, 0x15d8, 0x15e3, 0x15df, 0x15e0, 0x15e1, 0x15e2, 0x0d4c, 0x0d4d, 0x0d4e,
        0x0d4f, 0x0d53, 0x0d55, 0x15fb, 0x15fc, 0x15f9, 0x15fa, 0x1a1c, 0x1a1d, 0x1a1e, 0x1a1f, 0x0dc5, 0x0dc6,
        0x0dc7, 0x0dc8, 0x550a, 0x550b, 0x550c, 0x550d};
    int found = 0;
    for (unsigned int i = 0; i < sizeof(known) / sizeof(known[0]); i++) if (known[i] == device) found = 1;
    if ((id & 0xffff) != 0x8086 || !found) return -1;
    unsigned int bar = storage_pci_mmio_bar(pci & 0x00ffff00U, 0);
    if (!bar || linux_memory_map_mmio(bar, 0x20000)) return -1;
    net_pci_write(pci, 4, (net_pci_read(pci, 4) & 0xffff) | 6);   /* memory, bus master */
    base = (volatile unsigned char *)bar;
    w32(0xd8, 0xffffffffU);                           /* no interrupts */
    w32(0x00, r32(0x00) | (1U << 26));                /* reset */
    net_delay_ms(20);
    for (int i = 0; i < 100 && (r32(0x00) & (1U << 26)); i++) net_delay_ms(1);
    w32(0xd8, 0xffffffffU);
    w32(0x00, (r32(0x00) | (1U << 6) | (1U << 5)) & ~((1U << 3) | (1U << 31) | (1U << 7) | (1U << 30)));
    unsigned int low = r32(0x5400), high = r32(0x5404);
    if (!(high & (1U << 31)) || (!low && !(high & 0xffff))) {   /* address from the EEPROM */
        unsigned int a = eeprom(0), b = eeprom(1), c = eeprom(2);
        low = a | b << 16; high = c | (1U << 31);
        w32(0x5400, low); w32(0x5404, high);
    }
    for (int i = 0; i < 6; i++) driver->mac[i] = (unsigned char)((i < 4 ? low >> (8 * i) : high >> (8 * (i - 4))) & 255);
    for (unsigned int i = 0; i < 128; i++) w32(0x5200 + 4 * i, 0);
    for (int i = 0; i < RX_COUNT; i++) { memset(&rx[i], 0, sizeof(Rx)); rx[i].address = (unsigned int)rx_buffer[i]; }
    for (int i = 0; i < TX_COUNT; i++) { memset(&tx[i], 0, sizeof(Tx)); tx[i].status = 1; }
    w32(0x2800, (unsigned int)rx); w32(0x2804, 0); w32(0x2808, sizeof(rx));
    w32(0x2810, 0); w32(0x2818, RX_COUNT - 1);
    w32(0x3800, (unsigned int)tx); w32(0x3804, 0); w32(0x3808, sizeof(tx));
    w32(0x3810, 0); w32(0x3818, 0);
    rx_next = tx_next = 0;
    w32(0x100, (1U << 1) | (1U << 15) | (1U << 26));                 /* receive: broadcast, strip CRC, 2 KiB */
    w32(0x410, 0x0060200a);
    w32(0x400, (1U << 1) | (1U << 3) | (0x0fU << 4) | (0x40U << 12)); /* transmit, pad short packets */
    strcpy(driver->name, "Intel PRO/1000");
    if (device == 0x10d3) strcpy(driver->name, "Intel 82574 (e1000e)");
    else if (device >= 0x1500 || device < 0x1000) strcpy(driver->name, "Intel Ethernet (PCH)");
    driver->send = send; driver->receive = receive; driver->link = link;
    return 0;
}
