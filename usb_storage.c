#include "usb_storage.h"
#include "barnix.h"
#include <stdint.h>

/* UHCI DMA descriptors use physical addresses. Barnix has identity addressing
 * and no paging; all buffers below are static, aligned, and below 4 GiB. */
typedef struct { volatile uint32_t link, status, token, buffer; } TD;
typedef struct { volatile uint32_t link, element, reserved[2]; } QH;
static uint32_t frames[1024] __attribute__((aligned(4096)));
static TD transfers[512] __attribute__((aligned(16)));
static QH queue __attribute__((aligned(16)));
static unsigned char dma[4096] __attribute__((aligned(4096)));
static unsigned short io_base, root_port;
static unsigned int capacity, tag, generation;
static unsigned char address, ep0_size, bulk_in, bulk_out, in_size, out_size;
static unsigned char in_toggle, out_toggle, interface_number;
static int controller_started, ready;

#define ACTIVE (1U << 23)
#define ERRORS ((1U << 22) | (1U << 21) | (1U << 20) | (1U << 18) | (1U << 17))
#define PID_IN 0x69
#define PID_OUT 0xE1
#define PID_SETUP 0x2D

static unsigned short in16(unsigned short port)
{ unsigned short v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v; }
static unsigned int in32(unsigned short port)
{ unsigned int v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static void out8(unsigned short port, unsigned char v)
{ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port):"memory"); }
static void out16(unsigned short port, unsigned short v)
{ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port):"memory"); }
static void out32(unsigned short port, unsigned int v)
{ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port):"memory"); }

/* Keep USB reset and transfer delays bounded.  The kernel does not yet expose
 * a calibrated timer, so use a short polling delay instead of relying on PIT
 * channel 2 gate state (which is not consistent across emulators). */
static int delay_ms(unsigned int milliseconds)
{
    for (unsigned int ms = 0; ms < milliseconds; ms++)
        for (volatile unsigned int spins = 0; spins < 50000; spins++) {}
    return 0;
}
static uint32_t pci_read(unsigned int bus, unsigned int slot, unsigned int fn, unsigned int reg)
{
    out32(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & ~3U));
    return in32(0xCFC);
}
static void pci_write16(unsigned int bus, unsigned int slot, unsigned int fn,
                        unsigned int reg, unsigned short value)
{
    out32(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & ~3U));
    out16(0xCFC + (reg & 2), value);
}
static int start_controller(void)
{
    if (controller_started) return 0;
    for (unsigned int bus = 0; bus < 256; bus++)
        for (unsigned int slot = 0; slot < 32; slot++)
        {
            if ((pci_read(bus, slot, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            unsigned int functions = (pci_read(bus, slot, 0, 12) & 0x800000) ? 8 : 1;
            for (unsigned int fn = 0; fn < functions; fn++)
            {
                if ((pci_read(bus, slot, fn, 8) >> 8) != 0x0C0300) continue;
                unsigned int bar = pci_read(bus, slot, fn, 0x20);
                if (!(bar & 1) || !(bar & 0xFFFC) || (bar & 0xFFFF0000)) continue;
                io_base = bar & 0xFFFC;
                pci_write16(bus, slot, fn, 4, pci_read(bus, slot, fn, 4) | 5);
                /* Disable legacy SMI/trap routing and clear legacy status. */
                pci_write16(bus, slot, fn, 0xC0, 0x8F00);
                out16(io_base, 2);
                for (int i = 0; i < 100 && (in16(io_base) & 2); i++)
                    if (delay_ms(1)) return -1;
                if (in16(io_base) & 2) return -1;
                out16(io_base + 4, 0);
                out16(io_base + 2, 0x3F);
                queue.link = 1; queue.element = 1;
                for (int i = 0; i < 1024; i++) frames[i] = (uint32_t)&queue | 2;
                out16(io_base + 6, 0);
                out32(io_base + 8, (uint32_t)frames);
                out8(io_base + 12, 64);
                out16(io_base, 0xC1); /* Run, configured, 64-byte packets. */
                if (delay_ms(2)) return -1;
                if (in16(io_base + 2) & 0x20) return -1;
                controller_started = 1;
                return 0;
            }
        }
    return -1;
}
static int connected(void)
{
    unsigned short status = in16(root_port);
    return (status & 5) == 5 && !(status & 2);
}
int usb_storage_present(void)
{
    if (!ready || !connected()) { ready = 0; return 0; }
    return 1;
}
unsigned int usb_storage_sectors(void) { return capacity; }
unsigned int usb_storage_generation(void) { return generation; }

/* One synchronous packet chain, maximum 4096 bytes. Short IN packets stop
 * the chain; all descriptors are detached before their buffers can be reused. */
static int transfer(unsigned char pid, unsigned char endpoint, unsigned char max_packet,
                    unsigned char *toggle, void *buffer, unsigned int length)
{
    if (!connected() || !max_packet || length > sizeof(dma)) return -1;
    unsigned int count = length ? (length + max_packet - 1) / max_packet : 1;
    if (count > sizeof(transfers) / sizeof(transfers[0])) return -1;
    if (pid != PID_IN && length) memcpy(dma, buffer, length);
    unsigned int remaining = length, offset = 0;
    for (unsigned int i = 0; i < count; i++)
    {
        unsigned int packet = remaining > max_packet ? max_packet : remaining;
        transfers[i].link = i + 1 == count ? 1 : (uint32_t)&transfers[i + 1] | 4;
        transfers[i].status = ACTIVE | (3U << 27) | (1U << 29) | 0x7FF;
        transfers[i].token = pid | ((unsigned int)address << 8) | ((unsigned int)endpoint << 15) |
                             ((unsigned int)((*toggle + i) & 1) << 19) | (((packet - 1) & 0x7FF) << 21);
        transfers[i].buffer = (uint32_t)(dma + offset);
        offset += packet; remaining -= packet;
    }
    __asm__ volatile("" ::: "memory");
    queue.element = (uint32_t)transfers;
    unsigned int completed = 0, bytes = 0;
    int result = -1;
    for (unsigned int ms = 0; ms < 200; ms++)
    {
        if (!connected() || (in16(io_base + 2) & 0x38)) break;
        while (completed < count)
        {
            unsigned int status = transfers[completed].status;
            if (status & ACTIVE) break;
            if (status & ERRORS) goto done;
            unsigned int actual = (status + 1) & 0x7FF;
            unsigned int requested = ((transfers[completed].token >> 21) + 1) & 0x7FF;
            if (actual > requested) goto done;
            bytes += actual; completed++;
            if (actual < requested || completed == count) { result = bytes; goto done; }
        }
        if (delay_ms(1)) break;
    }
done:
    queue.element = 1;
    __asm__ volatile("" ::: "memory");
    if (delay_ms(2)) result = -1;
    if (result >= 0)
    {
        *toggle = (*toggle + completed) & 1;
        if (pid == PID_IN && bytes) memcpy(buffer, dma, bytes);
    }
    return result;
}
static int control(unsigned char type, unsigned char request, unsigned short value,
                   unsigned short index, void *buffer, unsigned short length)
{
    unsigned char setup[8] = {type, request, value, value >> 8, index, index >> 8,
                              length, length >> 8};
    unsigned char toggle = 0;
    if (transfer(PID_SETUP, 0, ep0_size, &toggle, setup, 8) != 8) return -1;
    int result = 0;
    toggle = 1;
    if (length)
    {
        result = transfer(type & 0x80 ? PID_IN : PID_OUT, 0, ep0_size, &toggle, buffer, length);
        if (result < 0) return -1;
    }
    toggle = 1;
    if (transfer(type & 0x80 ? PID_OUT : PID_IN, 0, ep0_size, &toggle, NULL, 0)) return -1;
    return result;
}
static unsigned int little32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
static unsigned int big32(const unsigned char *p)
{ return p[3] | ((unsigned int)p[2] << 8) | ((unsigned int)p[1] << 16) | ((unsigned int)p[0] << 24); }
static void store32(unsigned char *p, unsigned int n)
{ p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24; }

/* USB Mass Storage Bulk-Only Transport, transparent SCSI, LUN 0. */
static int command(const unsigned char *cdb, unsigned int cdb_length, void *data,
                   unsigned int size, int input)
{
    unsigned char cbw[31], csw[13];
    memset(cbw, 0, sizeof(cbw));
    store32(cbw, 0x43425355); store32(cbw + 4, ++tag); store32(cbw + 8, size);
    cbw[12] = input ? 0x80 : 0; cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);
    if (transfer(PID_OUT, bulk_out, out_size, &out_toggle, cbw, sizeof(cbw)) != sizeof(cbw)) goto error;
    if (size && transfer(input ? PID_IN : PID_OUT, input ? bulk_in : bulk_out,
                         input ? in_size : out_size, input ? &in_toggle : &out_toggle,
                         data, size) != (int)size) goto error;
    if (transfer(PID_IN, bulk_in, in_size, &in_toggle, csw, sizeof(csw)) != sizeof(csw) ||
        little32(csw) != 0x53425355 || little32(csw + 4) != tag ||
        little32(csw + 8) > size || csw[12] > 1) goto error;
    if (!csw[12] && little32(csw + 8)) goto error;
    return csw[12];
error:
    ready = 0; /* Next mount resets/re-enumerates the port, including BOT state. */
    return -1;
}
static int enumerate_port(unsigned short port)
{
    root_port = port;
    unsigned short status = in16(port);
    if (!(status & 1) || (status & 0x100)) return -1; /* No low-speed bulk devices. */
    out16(port, (status & ~0x100F) | 0x200);
    if (delay_ms(50)) return -1;
    out16(port, in16(port) & ~0x200);
    if (delay_ms(10)) return -1;
    out16(port, (in16(port) & ~0x100A) | 0xE);
    if (delay_ms(10) || !connected()) return -1;
    address = 0; ep0_size = 8; bulk_in = bulk_out = 0;
    in_toggle = out_toggle = 0;
    unsigned char descriptor[512];
    if (control(0x80, 6, 0x100, 0, descriptor, 8) != 8) return -1;
    ep0_size = descriptor[7];
    if (ep0_size != 8 && ep0_size != 16 && ep0_size != 32 && ep0_size != 64) return -1;
    if (control(0, 5, 1, 0, NULL, 0)) return -1;
    address = 1;
    if (delay_ms(3)) return -1;
    if (control(0x80, 6, 0x200, 0, descriptor, 9) != 9 || descriptor[1] != 2) return -1;
    unsigned int total = descriptor[2] | ((unsigned int)descriptor[3] << 8);
    if (total < 9 || total > sizeof(descriptor) ||
        control(0x80, 6, 0x200, 0, descriptor, total) != (int)total) return -1;
    unsigned char configuration = descriptor[5];
    int selected = 0;
    for (unsigned int offset = 0; offset + 2 <= total;)
    {
        unsigned char *d = descriptor + offset;
        if (d[0] < 2 || d[0] > total - offset) return -1;
        if (d[1] == 4)
        {
            if (bulk_in && bulk_out) break;
            selected = d[0] >= 9 && d[3] == 0 && d[5] == 8 && d[6] == 6 && d[7] == 0x50;
            bulk_in = bulk_out = 0;
            if (selected) interface_number = d[2];
        }
        if (selected && d[1] == 5 && d[0] >= 7 && (d[3] & 3) == 2)
        {
            unsigned int packet = d[4] | ((unsigned int)d[5] << 8);
            if (!packet || packet > 64 || !(d[2] & 15)) return -1;
            if (d[2] & 0x80) { bulk_in = d[2] & 15; in_size = packet; }
            else { bulk_out = d[2] & 15; out_size = packet; }
        }
        offset += d[0];
    }
    if (!bulk_in || !bulk_out || control(0, 9, configuration, 0, NULL, 0)) return -1;
    if (delay_ms(10)) return -1;
    /* Explicit BOT reset also establishes a known protocol state on remount. */
    if (control(0x21, 0xFF, 0, interface_number, NULL, 0)) return -1;
    unsigned char test[6] = {0}, sense[6] = {3, 0, 0, 0, 18, 0};
    int available = 0;
    for (int attempt = 0; attempt < 5; attempt++)
    {
        int result = command(test, sizeof(test), NULL, 0, 0);
        if (result == 0) { available = 1; break; }
        if (result < 0 || command(sense, sizeof(sense), descriptor, 18, 1) < 0) return -1;
        if (delay_ms(50)) return -1;
    }
    if (!available) return -1;
    unsigned char capacity_command[10] = {0x25};
    if (command(capacity_command, 10, descriptor, 8, 1) || big32(descriptor + 4) != 512 ||
        big32(descriptor) == 0xFFFFFFFFU) return -1;
    capacity = big32(descriptor) + 1;
    generation++;
    ready = 1;
    return 0;
}
int usb_storage_probe(void)
{
    if (usb_storage_present()) return 0;
    if (start_controller()) return -1;
    /* Only one device address is in use. Disable both ports before probing. */
    for (int p = 0; p < 2; p++) out16(io_base + 0x10 + p * 2, in16(io_base + 0x10 + p * 2) & ~4U);
    for (int p = 0; p < 2; p++)
    {
        if (enumerate_port(io_base + 0x10 + p * 2) == 0) return 0;
        out16(io_base + 0x10 + p * 2, in16(io_base + 0x10 + p * 2) & ~4U);
    }
    ready = 0;
    return -1;
}
static int sectors_command(unsigned int lba, void *buffer, unsigned int count, int input)
{
    if (!usb_storage_present() || !count || count > 8 || lba >= capacity || count > capacity - lba)
        return -1;
    unsigned char cdb[10] = {input ? 0x28 : 0x2A, 0, lba >> 24, lba >> 16, lba >> 8, lba,
                             0, count >> 8, count, 0};
    return command(cdb, sizeof(cdb), buffer, count * 512, input) == 0 ? 0 : -1;
}
int usb_storage_read(unsigned int lba, void *buffer, unsigned int count)
{ return sectors_command(lba, buffer, count, 1); }
int usb_storage_write(unsigned int lba, const void *buffer, unsigned int count)
{ return sectors_command(lba, (void *)buffer, count, 0); }
int usb_storage_flush(void)
{
    if (!usb_storage_present()) return -1;
    unsigned char cdb[10] = {0x35};
    return command(cdb, sizeof(cdb), NULL, 0, 0) == 0 ? 0 : -1;
}
