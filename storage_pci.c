#include "storage_pci.h"
#include "barnix.h"
#include "linux_memory.h"

/* Polling drivers with identity-mapped DMA below 4 GiB. All transfers are
 * synchronous; IRQ/MSI, IOMMU and controller hotplug are not required. */
#define WAIT_LIMIT 10000000U
#define CONTROLLERS 8
#define QDEPTH 16
static inline unsigned char in8(unsigned short p) { unsigned char v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline unsigned short in16(unsigned short p) { unsigned short v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline unsigned int in32(unsigned short p) { unsigned int v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void out8(unsigned short p, unsigned char v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void out16(unsigned short p, unsigned short v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void out32(unsigned short p, unsigned int v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void barrier(void) { __asm__ volatile("":::"memory"); }
static unsigned int mmread(unsigned int base, unsigned int off) { return *(volatile unsigned int *)(base + off); }
static void mmwrite(unsigned int base, unsigned int off, unsigned int value) { *(volatile unsigned int *)(base + off) = value; }
static void mmaddress(unsigned int base, unsigned int off, const void *address)
{ mmwrite(base, off, (unsigned int)address); mmwrite(base, off + 4, 0); }
static unsigned int pci_read(unsigned int pci, unsigned int off)
{ out32(0xcf8, 0x80000000U | pci | (off & ~3U)); return in32(0xcfc); }
static void pci_enable(unsigned int pci)
{ out32(0xcf8, 0x80000004U | pci); out16(0xcfc, (unsigned short)pci_read(pci, 4) | 7); }
static unsigned int pci_byte(unsigned int pci, unsigned int off)
{ return (pci_read(pci, off) >> ((off & 3) * 8)) & 255; }
/* EFI may assign 64-bit BARs above 4 GiB. For root-bus devices, reserve a
 * conflict-free low MMIO range before enabling the driver. Never move bridge
 * children without a bridge-window allocator. Firmware RAM/reserved ranges
 * and every assigned PCI BAR/window remain excluded. */
static struct { uint64_t first, end; } pci_reserved[256];
static unsigned int pci_reserved_count;
static int pci_allocator_ready;
static void pci_write(unsigned int pci, unsigned int off, unsigned int value)
{ out32(0xcf8, 0x80000000U | pci | (off & ~3U)); out32(0xcfc, value); }
static void pci_command(unsigned int pci, unsigned short value)
{ out32(0xcf8, 0x80000004U | pci); out16(0xcfc, value); }
static void reserve_mmio(uint64_t first, uint64_t bytes)
{
    if (!bytes || first >= 0x100000000ULL) return;
    if (pci_reserved_count == 256 || first + bytes < first) { pci_allocator_ready = 0; return; }
    pci_reserved[pci_reserved_count].first = first;
    pci_reserved[pci_reserved_count++].end = first + bytes;
}
static uint64_t bar_size(unsigned int pci, unsigned int bar, unsigned int value)
{
    unsigned int offset = 0x10 + bar * 4;
    unsigned short command = pci_read(pci, 4);
    unsigned int high = (value & 6) == 4 ? pci_read(pci, offset + 4) : 0;
    pci_command(pci, command & ~7U);
    pci_write(pci, offset, 0xffffffffU);
    if ((value & 6) == 4) pci_write(pci, offset + 4, 0xffffffffU);
    uint64_t mask = pci_read(pci, offset) & ~15U;
    if ((value & 6) == 4) mask |= (uint64_t)pci_read(pci, offset + 4) << 32;
    else mask |= 0xffffffff00000000ULL;
    pci_write(pci, offset, value);
    if ((value & 6) == 4) pci_write(pci, offset + 4, high);
    pci_command(pci, command);
    return ~mask + 1;
}
void storage_pci_boot_memory(const MultibootInfo *info)
{
    if (!info || !(info->flags & (1U << 6)) || !info->mmap_addr || info->mmap_length > 65536) return;
    pci_allocator_ready = 1;
    if (info->flags & (1U << 12)) reserve_mmio(info->framebuffer_low | ((uint64_t)info->framebuffer_high << 32),
                                            (uint64_t)info->framebuffer_pitch * info->framebuffer_height);
    for (unsigned int off = 0; off < info->mmap_length;) {
        const unsigned int *record = (const unsigned int *)(info->mmap_addr + off);
        if (info->mmap_length - off < 24 || record[0] < 20 || record[0] > info->mmap_length - off - 4) {
            pci_allocator_ready = 0; return;
        }
        reserve_mmio(record[1] | ((uint64_t)record[2] << 32), record[3] | ((uint64_t)record[4] << 32));
        off += record[0] + 4;
    }
    for (unsigned int bus = 0; bus < 256; bus++) for (unsigned int slot = 0; slot < 32; slot++) {
        unsigned int base = (bus << 16) | (slot << 11);
        if ((pci_read(base, 0) & 65535) == 65535) continue;
        unsigned int functions = pci_read(base, 12) & 0x800000 ? 8 : 1;
        for (unsigned int f = 0; f < functions; f++) {
            unsigned int pci = base | (f << 8);
            if ((pci_read(pci, 0) & 65535) == 65535) continue;
            unsigned int header = (pci_read(pci, 12) >> 16) & 127;
            unsigned int bars = header == 0 ? 6 : header == 1 ? 2 : 0;
            for (unsigned int b = 0; b < bars; b++) {
                unsigned int value = pci_read(pci, 0x10 + b * 4);
                if (!value || (value & 1) || ((value & 6) == 4 && b + 1 == bars)) continue;
                uint64_t address = value & ~15U, size = bar_size(pci, b, value);
                if ((value & 6) == 4) address |= (uint64_t)pci_read(pci, 0x14 + 4 * b++) << 32;
                if (address) reserve_mmio(address, size);
            }
            if (header == 1) {
                unsigned int window = pci_read(pci, 0x20);
                uint64_t start = (window & 0xfff0U) << 16, end = (window & 0xfff00000U) + 0x100000ULL;
                if (end > start) reserve_mmio(start, end - start);
                window = pci_read(pci, 0x24);
                start = (window & 0xfff0U) << 16; end = (window & 0xfff00000U) + 0x100000ULL;
                if ((window & 15) == 1) {
                    start |= (uint64_t)pci_read(pci, 0x28) << 32;
                    end += (uint64_t)pci_read(pci, 0x2c) << 32;
                }
                if (end > start) reserve_mmio(start, end - start);
            }
        }
    }
}
unsigned int storage_pci_mmio_bar(unsigned int pci, unsigned int bar)
{
    if (bar > 5) return 0;
    unsigned int value = pci_read(pci, 0x10 + bar * 4);
    if (!value || (value & 1)) return 0;
    if ((value & 6) == 4) {
        if (bar == 5) return 0;
        if (pci_read(pci, 0x14 + bar * 4)) {
            if (!pci_allocator_ready || (pci >> 16) || pci_reserved_count == 256) return 0;
            uint64_t size = bar_size(pci, bar, value);
            if (!size || size > 16 * 1024 * 1024 || (size & (size - 1))) return 0;
            uint64_t candidate = (0x90000000ULL + size - 1) & ~(size - 1);
            for (;;) {
                int collision = 0;
                if (candidate + size > 0xf0000000ULL) return 0;
                for (unsigned int i = 0; i < pci_reserved_count; i++)
                    if (candidate < pci_reserved[i].end && candidate + size > pci_reserved[i].first) {
                        candidate = (pci_reserved[i].end + size - 1) & ~(size - 1);
                        collision = 1; break;
                    }
                if (!collision) break;
            }
            unsigned short command = pci_read(pci, 4);
            unsigned int old_high = pci_read(pci, 0x14 + bar * 4);
            pci_command(pci, command & ~7U);
            pci_write(pci, 0x10 + bar * 4, (unsigned int)candidate | (value & 15));
            pci_write(pci, 0x14 + bar * 4, 0);
            if ((pci_read(pci, 0x10 + bar * 4) & ~15U) != candidate || pci_read(pci, 0x14 + bar * 4)) {
                pci_write(pci, 0x10 + bar * 4, value); pci_write(pci, 0x14 + bar * 4, old_high);
                pci_command(pci, command); return 0;
            }
            reserve_mmio(candidate, size); pci_command(pci, command);
            return candidate;
        }
    }
    return value & ~15U;
}
static int wait_bits(unsigned int base, unsigned int off, unsigned int mask, unsigned int expected)
{
    for (unsigned int i = 0; i < WAIT_LIMIT; i++) if ((mmread(base, off) & mask) == expected) return 0;
    return -1;
}
static unsigned int le32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
static uint64_t le64(const unsigned char *p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }
enum { SATA = 1, NVME, VIRTIO };
typedef struct {
    int kind, controller, ready, readonly;
    unsigned int port, nsid, sector_shift;
    uint64_t sectors;
    char name[24];
} Device;
static Device devices[PCI_STORAGE_MAX];
static int device_count, initialized;
static unsigned int sata_count;
static unsigned char bounce[4096] __attribute__((aligned(4096)));
static void indexed_name(char *out, const char *prefix, unsigned int n)
{ strcpy(out, prefix); utoa(n, out + strlen(out)); }

/* AHCI: one command slot per port, one contiguous PRDT entry. */
static unsigned char ahci_lists[PCI_STORAGE_MAX][1024] __attribute__((aligned(1024)));
static unsigned char ahci_fis[PCI_STORAGE_MAX][256] __attribute__((aligned(256)));
static unsigned char ahci_tables[PCI_STORAGE_MAX][256] __attribute__((aligned(128)));
static int ahci_command(Device *d, unsigned char command, unsigned int lba, unsigned int count, void *buffer, unsigned int bytes, int write)
{
    unsigned int port = d->port, index = d - devices;
    if (!d->ready || (mmread(port, 0x28) & 15) != 3 || wait_bits(port, 0x20, 0x88, 0)) return -1;
    unsigned int *header = (unsigned int *)ahci_lists[index];
    unsigned char *table = ahci_tables[index];
    memset(header, 0, 32); memset(table, 0, 256);
    header[0] = 5 | (write ? 1U << 6 : 0) | (bytes ? 1U << 16 : 0);
    header[2] = (unsigned int)table;
    table[0] = 0x27; table[1] = 0x80; table[2] = command;
    table[4] = lba; table[5] = lba >> 8; table[6] = lba >> 16; table[7] = 0x40;
    table[8] = lba >> 24; table[12] = count; table[13] = count >> 8;
    if (bytes) {
        unsigned int *prdt = (unsigned int *)(table + 128);
        prdt[0] = (unsigned int)buffer; prdt[3] = bytes - 1;
    }
    mmwrite(port, 0x10, 0xffffffffU); mmwrite(port, 0x30, 0xffffffffU);
    barrier(); mmwrite(port, 0x38, 1);
    for (unsigned int i = 0; i < WAIT_LIMIT; i++) {
        if (mmread(port, 0x10) & (1U << 30)) return -1;
        if (!(mmread(port, 0x38) & 1)) { barrier(); return mmread(port, 0x20) & 1 ? -1 : 0; }
    }
    mmwrite(port, 0x18, mmread(port, 0x18) & ~1U); d->ready = 0; return -1;
}
static void ahci_init(unsigned int pci)
{
    unsigned int base = storage_pci_mmio_bar(pci, 5);
    if (!base || linux_memory_map_mmio(base, 0x1100)) return;
    pci_enable(pci);
    if (mmread(base, 0x24) & 1) {
        mmwrite(base, 0x28, mmread(base, 0x28) | 2);
        if (wait_bits(base, 0x28, 0x11, 0)) return;
    }
    mmwrite(base, 4, (mmread(base, 4) | (1U << 31)) & ~2U);
    unsigned int ports = mmread(base, 0x0c);
    for (unsigned int p = 0; p < 32 && device_count < PCI_STORAGE_MAX; p++) {
        unsigned int port = base + 0x100 + p * 0x80;
        if (!(ports & (1U << p)) || (mmread(port, 0x28) & 0xf0f) != 0x103 || mmread(port, 0x24) != 0x101) continue;
        mmwrite(port, 0x18, mmread(port, 0x18) & ~1U);
        if (wait_bits(port, 0x18, 1U << 15, 0)) continue;
        mmwrite(port, 0x18, mmread(port, 0x18) & ~(1U << 4));
        if (wait_bits(port, 0x18, 1U << 14, 0)) continue;
        Device *d = &devices[device_count]; memset(d, 0, sizeof(*d));
        d->kind = SATA; d->port = port; d->ready = 1; d->sector_shift = 9;
        memset(ahci_lists[device_count], 0, 1024); memset(ahci_fis[device_count], 0, 256);
        mmaddress(port, 0, ahci_lists[device_count]); mmaddress(port, 8, ahci_fis[device_count]);
        mmwrite(port, 0x14, 0); mmwrite(port, 0x30, 0xffffffffU); mmwrite(port, 0x10, 0xffffffffU);
        mmwrite(port, 0x18, mmread(port, 0x18) | 0x11);
        if (ahci_command(d, 0xec, 0, 0, bounce, 512, 0)) { d->ready = 0; continue; }
        unsigned short *id = (unsigned short *)bounce;
        if (!(id[83] & (1U << 10))) { d->ready = 0; continue; }
        uint64_t native = le64(bounce + 200);
        if ((id[106] & 0xd000) == 0x5000) {
            unsigned int size = le32(bounce + 234) * 2;
            if (size != 512 && size != 1024 && size != 2048 && size != 4096) { d->ready = 0; continue; }
            d->sector_shift = size == 4096 ? 12 : size == 2048 ? 11 : size == 1024 ? 10 : 9;
        }
        d->sectors = native << (d->sector_shift - 9);
        indexed_name(d->name, "sata", sata_count++); device_count++;
    }
}

/* NVMe: a polling admin queue and one I/O queue per controller. */
typedef struct {
    unsigned int base, stride, cid;
    unsigned short tail[2], head[2]; unsigned char phase[2];
    struct {
        unsigned int sq[QDEPTH][16] __attribute__((aligned(4096)));
        volatile unsigned int cq[QDEPTH][4] __attribute__((aligned(4096)));
    } queues[2];
} Nvme;
static Nvme nvme[CONTROLLERS] __attribute__((aligned(4096)));
static unsigned int nvme_count;
static int nvme_command(Nvme *n, int queue, unsigned int *cmd)
{
    unsigned int cid = ++n->cid & 65535;
    cmd[0] = (cmd[0] & 65535) | (cid << 16);
    memcpy(n->queues[queue].sq[n->tail[queue]], cmd, 64);
    n->tail[queue] = (n->tail[queue] + 1) % QDEPTH;
    barrier(); mmwrite(n->base, 0x1000 + queue * 2 * n->stride, n->tail[queue]);
    volatile unsigned int *entry = n->queues[queue].cq[n->head[queue]];
    for (unsigned int i = 0; i < WAIT_LIMIT; i++) {
        if (((entry[3] >> 16) & 1) != n->phase[queue]) continue;
        barrier(); unsigned int status = entry[3];
        n->head[queue] = (n->head[queue] + 1) % QDEPTH;
        if (!n->head[queue]) n->phase[queue] ^= 1;
        mmwrite(n->base, 0x1000 + (queue * 2 + 1) * n->stride, n->head[queue]);
        return (status & 65535) == cid && !(status >> 17) ? 0 : -1;
    }
    mmwrite(n->base, 0x14, 0); wait_bits(n->base, 0x1c, 1, 0);
    for (int i = 0; i < device_count; i++) if (devices[i].kind == NVME && devices[i].controller == (int)(n - nvme)) devices[i].ready = 0;
    return -1;
}
static void nvme_init(unsigned int pci)
{
    if (nvme_count == CONTROLLERS || device_count == PCI_STORAGE_MAX) return;
    unsigned int base = storage_pci_mmio_bar(pci, 0); if (!base || linux_memory_map_mmio(base, 4096)) return;
    pci_enable(pci);
    unsigned int cap = mmread(base, 0), high = mmread(base, 4);
    if ((cap & 65535) < QDEPTH - 1 || ((high >> 16) & 15) || !(high & 0x20)) return;
    mmwrite(base, 0x14, 0); if (wait_bits(base, 0x1c, 1, 0)) return;
    Nvme *n = &nvme[nvme_count]; memset(n, 0, sizeof(*n));
    n->base = base; n->stride = 4U << (high & 15); n->phase[0] = n->phase[1] = 1;
    if (linux_memory_map_mmio(base + 4096, 4 * n->stride)) return;
    mmwrite(base, 0x0c, 0xffffffffU);
    mmwrite(base, 0x24, (QDEPTH - 1) | ((QDEPTH - 1) << 16));
    mmaddress(base, 0x28, n->queues[0].sq); mmaddress(base, 0x30, (const void *)n->queues[0].cq);
    mmwrite(base, 0x14, 1 | (6U << 16) | (4U << 20));
    if (wait_bits(base, 0x1c, 3, 1)) return;
    unsigned int cmd[16] = {0};
    cmd[0] = 5; cmd[6] = (unsigned int)n->queues[1].cq; cmd[10] = 1 | ((QDEPTH - 1) << 16); cmd[11] = 1;
    if (nvme_command(n, 0, cmd)) return;
    memset(cmd, 0, sizeof(cmd)); cmd[0] = 1; cmd[6] = (unsigned int)n->queues[1].sq;
    cmd[10] = 1 | ((QDEPTH - 1) << 16); cmd[11] = 1 | (1U << 16);
    if (nvme_command(n, 0, cmd)) return;
    memset(cmd, 0, sizeof(cmd)); cmd[0] = 6; cmd[6] = (unsigned int)bounce; cmd[10] = 2;
    if (nvme_command(n, 0, cmd)) return;
    unsigned int ids[PCI_STORAGE_MAX];
    memcpy(ids, bounce, sizeof(ids));
    for (unsigned int i = 0; i < PCI_STORAGE_MAX && ids[i] && device_count < PCI_STORAGE_MAX; i++) {
        memset(cmd, 0, sizeof(cmd)); cmd[0] = 6; cmd[1] = ids[i]; cmd[6] = (unsigned int)bounce;
        if (nvme_command(n, 0, cmd)) continue;
        unsigned int fmt = bounce[26] & 15, shift = bounce[128 + fmt * 4 + 2];
        if (fmt > bounce[25] || bounce[128 + fmt * 4] || bounce[129 + fmt * 4] || shift < 9 || shift > 12) continue;
        Device *d = &devices[device_count]; memset(d, 0, sizeof(*d));
        d->kind = NVME; d->controller = nvme_count; d->nsid = ids[i]; d->sector_shift = shift;
        d->sectors = le64(bounce) << (shift - 9); if (!d->sectors) continue;
        d->ready = 1; indexed_name(d->name, "nvme", nvme_count);
        strcat(d->name, "n"); utoa(ids[i], d->name + strlen(d->name)); device_count++;
    }
    nvme_count++;
}

/* VirtIO block, both PCI legacy and modern capabilities, split virtqueues. */
typedef struct {
    unsigned int io, common, notify, config, notify_multiplier;
    unsigned short size, available, used;
    unsigned char modern;
    unsigned char ring[12288] __attribute__((aligned(4096)));
    unsigned int request[4]; volatile unsigned char status;
} Virtio;
static Virtio virtio[CONTROLLERS] __attribute__((aligned(4096)));
static unsigned int virtio_count;
static void vstatus(Virtio *v, unsigned char value)
{ if (v->modern) *(volatile unsigned char *)(v->common + 20) = value; else out8(v->io + 18, value); }
static int virtio_request(Device *d, unsigned int lba, void *buffer, unsigned int bytes, int write, int flush)
{
    Virtio *v = &virtio[d->controller];
    unsigned int *desc = (unsigned int *)v->ring;
    unsigned int avail_offset = v->size * 16;
    unsigned int used_offset = (avail_offset + 6 + v->size * 2 + 4095) & ~4095U;
    volatile unsigned short *available = (volatile unsigned short *)(v->ring + avail_offset);
    volatile unsigned short *used = (volatile unsigned short *)(v->ring + used_offset);
    v->request[0] = flush ? 4 : write ? 1 : 0; v->request[1] = 0;
    v->request[2] = lba; v->request[3] = 0; v->status = 255;
    memset(desc, 0, 48);
    desc[0] = (unsigned int)v->request; desc[2] = 16; desc[3] = 1 | ((flush ? 2U : 1U) << 16);
    desc[4] = (unsigned int)buffer; desc[6] = bytes; desc[7] = 1 | (write ? 0 : 2) | (2U << 16);
    desc[8] = (unsigned int)&v->status; desc[10] = 1; desc[11] = 2;
    available[2 + v->available % v->size] = 0;
    barrier(); available[1] = ++v->available; barrier();
    if (v->modern) *(volatile unsigned short *)v->notify = 0; else out16(v->io + 16, 0);
    for (unsigned int i = 0; i < WAIT_LIMIT; i++) {
        if (used[1] == v->used) continue;
        barrier(); v->used = used[1]; return v->status == 0 ? 0 : -1;
    }
    vstatus(v, 0); d->ready = 0; return -1;
}
static void virtio_init(unsigned int pci)
{
    if (virtio_count == CONTROLLERS || device_count == PCI_STORAGE_MAX) return;
    Virtio *v = &virtio[virtio_count]; memset(v, 0, sizeof(*v));
    pci_enable(pci);
    unsigned int bar = pci_read(pci, 0x10);
    if ((bar & 1) && !(bar & 0xffff0000)) v->io = bar & ~3U;
    unsigned int cap = pci_byte(pci, 0x34) & ~3U;
    for (int i = 0; cap >= 0x40 && i < 48; i++) {
        if (pci_byte(pci, cap) == 9 && pci_byte(pci, cap + 2) >= 16) {
            unsigned int base = storage_pci_mmio_bar(pci, pci_byte(pci, cap + 4));
            unsigned int off = pci_read(pci, cap + 8), length = pci_read(pci, cap + 12);
            unsigned int type = pci_byte(pci, cap + 3);
            if (base && off <= 0xffffffffU - base) {
                if (type == 1 && length >= 56) v->common = base + off;
                if (type == 2 && pci_byte(pci, cap + 2) >= 20) { v->notify = base + off; v->notify_multiplier = pci_read(pci, cap + 16); }
                if (type == 4 && length >= 8) v->config = base + off;
            }
        }
        cap = pci_byte(pci, cap + 1) & ~3U;
    }
    v->modern = v->common && v->notify && v->config;
    if (v->modern && (linux_memory_map_mmio(v->common, 56) || linux_memory_map_mmio(v->config, 8))) return;
    if (!v->modern && !v->io) return;
    vstatus(v, 0); vstatus(v, 1); vstatus(v, 3);
    unsigned int features;
    uint64_t capacity;
    if (v->modern) {
        mmwrite(v->common, 0, 0); features = mmread(v->common, 4);
        mmwrite(v->common, 0, 1); if (!(mmread(v->common, 4) & 1)) return;
        mmwrite(v->common, 8, 0); mmwrite(v->common, 12, features & (1U << 9)); /* FLUSH */
        mmwrite(v->common, 8, 1); mmwrite(v->common, 12, 1); /* VERSION_1 */
        vstatus(v, 11); if (!(*(volatile unsigned char *)(v->common + 20) & 8)) return;
        *(volatile unsigned short *)(v->common + 22) = 0;
        v->size = *(volatile unsigned short *)(v->common + 24);
        if (v->size > 128) v->size = 128;
        if (v->size < 3 || (v->size & (v->size - 1))) return;
        *(volatile unsigned short *)(v->common + 24) = v->size;
        *(volatile unsigned short *)(v->common + 26) = 0xffff;
        mmaddress(v->common, 32, v->ring);
        mmaddress(v->common, 40, v->ring + v->size * 16);
        mmaddress(v->common, 48, v->ring + ((v->size * 18 + 6 + 4095) & ~4095U));
        v->notify += *(volatile unsigned short *)(v->common + 30) * v->notify_multiplier;
        if (linux_memory_map_mmio(v->notify, 2)) return;
        *(volatile unsigned short *)(v->common + 28) = 1;
        capacity = mmread(v->config, 0) | ((uint64_t)mmread(v->config, 4) << 32);
        vstatus(v, 15);
    } else {
        features = in32(v->io); out32(v->io + 4, features & (1U << 9));
        out16(v->io + 14, 0); v->size = in16(v->io + 12);
        if (v->size < 3 || v->size > 256 || (v->size & (v->size - 1))) return;
        out32(v->io + 8, (unsigned int)v->ring >> 12);
        capacity = in32(v->io + 20) | ((uint64_t)in32(v->io + 24) << 32);
        vstatus(v, 7);
    }
    if (!capacity) return;
    Device *d = &devices[device_count++]; memset(d, 0, sizeof(*d));
    d->kind = VIRTIO; d->controller = virtio_count; d->ready = 1; d->sectors = capacity; d->sector_shift = 9;
    d->readonly = !!(features & (1U << 5)); d->port = !!(features & (1U << 9));
    indexed_name(d->name, "virtio", virtio_count++);
}

void storage_pci_init(void)
{
    if (initialized) return;
    initialized = 1;
    for (unsigned int bus = 0; bus < 256; bus++) for (unsigned int slot = 0; slot < 32; slot++) {
        unsigned int pci = (bus << 16) | (slot << 11);
        if ((pci_read(pci, 0) & 65535) == 65535) continue;
        unsigned int functions = pci_read(pci, 12) & 0x800000 ? 8 : 1;
        for (unsigned int f = 0; f < functions; f++) {
            unsigned int address = pci | (f << 8), id = pci_read(address, 0), kind = pci_read(address, 8) >> 8;
            if (kind == 0x010601) ahci_init(address);
            else if (kind == 0x010802) nvme_init(address);
            else if ((id & 65535) == 0x1af4 && ((id >> 16) == 0x1001 || (id >> 16) == 0x1042)) virtio_init(address);
        }
    }
}
int storage_pci_count(void) { storage_pci_init(); return device_count; }
const char *storage_pci_name(int i) { return i >= 0 && i < device_count ? devices[i].name : "none"; }
const char *storage_pci_type(int i) { return devices[i].kind == SATA ? "sata" : devices[i].kind == NVME ? "nvme" : "virt"; }
uint64_t storage_pci_capacity(int i) { return devices[i].sectors; }
int storage_pci_present(int i) { return i >= 0 && i < device_count && devices[i].ready; }
static int native_io(Device *d, unsigned int lba, void *data, unsigned int count, int write)
{
    if (d->kind == SATA) return ahci_command(d, write ? 0x35 : 0x25, lba, count, data, count << d->sector_shift, write);
    if (d->kind == VIRTIO) return virtio_request(d, lba, data, count * 512, write, 0);
    unsigned int cmd[16] = {0}; cmd[0] = write ? 1 : 2; cmd[1] = d->nsid;
    cmd[6] = (unsigned int)data; cmd[10] = lba; cmd[12] = count - 1;
    return nvme_command(&nvme[d->controller], 1, cmd);
}
static int transfer(int index, unsigned int lba, void *data, unsigned int sectors, int write)
{
    if (!storage_pci_present(index) || !sectors || sectors > 8) return -1;
    Device *d = &devices[index];
    if ((uint64_t)lba + sectors > d->sectors || (write && d->readonly)) return -1;
    unsigned int ratio = 1U << (d->sector_shift - 9);
    unsigned char *bytes = data;
    while (sectors) {
        unsigned int native = lba / ratio, offset = lba % ratio, count = ratio - offset;
        if (count > sectors) count = sectors;
        if ((!write || offset || count != ratio) && native_io(d, native, bounce, 1, 0)) return -1;
        if (write) {
            memcpy(bounce + offset * 512, bytes, count * 512);
            if (native_io(d, native, bounce, 1, 1)) return -1;
        } else memcpy(bytes, bounce + offset * 512, count * 512);
        sectors -= count; lba += count; bytes += count * 512;
    }
    return 0;
}
int storage_pci_read(int i, unsigned int lba, void *data, unsigned int sectors) { return transfer(i, lba, data, sectors, 0); }
int storage_pci_write(int i, unsigned int lba, const void *data, unsigned int sectors) { return transfer(i, lba, (void *)data, sectors, 1); }
int storage_pci_flush(int i)
{
    if (!storage_pci_present(i)) return -1;
    Device *d = &devices[i];
    if (d->kind == SATA) return ahci_command(d, 0xea, 0, 0, NULL, 0, 0);
    if (d->kind == VIRTIO) return d->port ? virtio_request(d, 0, NULL, 0, 0, 1) : 0;
    unsigned int cmd[16] = {0}; cmd[1] = d->nsid;
    return nvme_command(&nvme[d->controller], 1, cmd);
}
