#include "net.h"

static void pause_io(void)
{ for (volatile unsigned int i = 0; i < 10000; i++) __asm__ volatile("pause"); }

typedef struct { unsigned int address; unsigned short length; unsigned short checksum;
                 unsigned char status, errors, special, reserved; } E1000Desc;
static E1000Desc rx_desc[16] __attribute__((aligned(16)));
static E1000Desc tx_desc[16] __attribute__((aligned(16)));
static unsigned char rx_buf[16][2048] __attribute__((aligned(16)));
static unsigned char tx_buf[16][2048] __attribute__((aligned(16)));
static unsigned int base, rx_head, tx_head;

static inline unsigned int reg_read(unsigned int reg)
{ return *(volatile unsigned int *)(base + reg); }
static inline void reg_write(unsigned int reg, unsigned int value)
{ *(volatile unsigned int *)(base + reg) = value; }

/* Intel PRO/1000 reset. Descriptor rings and packet transfer are provided by
 * the network layer above this hardware reset routine. */
int e1000_reset(unsigned int mmio)
{
    if (!mmio) return -1;
    base = mmio;
    *(volatile unsigned int *)(mmio + 0x0000) |= (1U << 26);
    pause_io();
    *(volatile unsigned int *)(mmio + 0x0004) = 0;
    for (unsigned int i = 0; i < 16; i++) {
        rx_desc[i].address = (unsigned int)rx_buf[i]; rx_desc[i].status = 0;
        tx_desc[i].address = (unsigned int)tx_buf[i]; tx_desc[i].status = 1;
    }
    reg_write(0x2800, (unsigned int)rx_desc); reg_write(0x2804, 0);
    reg_write(0x2808, 16 * sizeof(E1000Desc)); reg_write(0x280C, 0);
    reg_write(0x2810, 0); reg_write(0x2818, 16 * sizeof(E1000Desc));
    reg_write(0x3800, (unsigned int)tx_desc); reg_write(0x3804, 0);
    reg_write(0x3808, 16 * sizeof(E1000Desc)); reg_write(0x380C, 0);
    reg_write(0x3810, 0); reg_write(0x3818, 0);
    reg_write(0x100, 0x04000000U | 0x02U | 0x04U | 0x8000U); /* RCTL: receiver + broadcast */
    reg_write(0x400, 0x04000000U | 0x00000008U | 0x00000010U); /* TX */
    reg_write(0xD8, 16); /* receive tail */
    return 0;
}

int e1000_send(const void *packet, unsigned int length)
{
    if (!base || !packet || !length || length > 2048) return -1;
    E1000Desc *d = &tx_desc[tx_head];
    if (!(d->status & 1)) return -1;
    const unsigned char *src = packet;
    for (unsigned int i = 0; i < length; i++) tx_buf[tx_head][i] = src[i];
    d->length = (unsigned short)length; d->status = 0; d->errors = 0;
    d->reserved = 0; d->checksum = 0;
    d->special = 0; d->status = 1 | 2 | 8; /* EOP|IFCS|RS */
    tx_head = (tx_head + 1) & 15; reg_write(0x3818, tx_head); return 0;
}

int e1000_receive(void *packet, unsigned int capacity)
{
    if (!base || !packet || !capacity) return -1;
    E1000Desc *d = &rx_desc[rx_head];
    if (!(d->status & 1)) return 0;
    unsigned int n = d->length < capacity ? d->length : capacity;
    unsigned char *dst = packet; for (unsigned int i = 0; i < n; i++) dst[i] = rx_buf[rx_head][i];
    d->status = 0; rx_head = (rx_head + 1) & 15; reg_write(0xD8, rx_head); return (int)n;
}

int e1000_mac(unsigned char out[6])
{
    if (!base || !out) return -1;
    unsigned int low = reg_read(0x5400), high = reg_read(0x5404);
    out[0] = low; out[1] = low >> 8; out[2] = low >> 16; out[3] = low >> 24;
    out[4] = high; out[5] = high >> 8; return 0;
}

int e1000_link_up(void)
{
    return base && (reg_read(0x0008) & 0x02U) != 0;
}
