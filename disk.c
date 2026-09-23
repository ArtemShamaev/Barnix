#include "lang.h"
#include "disk.h"
#include "barnix.h"
#include "usb_storage.h"

#define DISK_SECTORS 4096

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CONTROL    0x3F6

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

static unsigned char ram_disk[DISK_SECTORS][DISK_SECTOR_SIZE];
static int ram_ready;
static DiskSelection selected = {DISK_NONE, 0, 0, 0};
static unsigned int ata_sectors;

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static int ata_wait_not_busy(void)
{
    for (int i = 0; i < 1000000; i++)
        if ((inb(ATA_STATUS) & ATA_SR_BSY) == 0)
            return 0;

    return -1;
}

static int ata_wait_drq(void)
{
    for (int i = 0; i < 1000000; i++)
    {
        unsigned char status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR)
            return -1;
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ))
            return 0;
    }

    return -1;
}

static int ata_select_master(void)
{
    outb(ATA_DRIVE, 0xE0);
    io_wait();
    return ata_wait_not_busy();
}

static int ata_identify(void)
{
    if (ata_select_master() != 0)
        return -1;

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    if (inb(ATA_STATUS) == 0)
        return -1;

    if (ata_wait_drq() != 0)
        return -1;

    unsigned short identity[256];
    for (int i = 0; i < 256; i++)
        identity[i] = inw(ATA_DATA);
    ata_sectors = identity[60] | ((unsigned int)identity[61] << 16);

    return 0;
}

static int ata_prepare_lba(unsigned int lba)
{
    if (lba >= (1U << 28))
        return -1;

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    return 0;
}

static int ata_read(unsigned int lba, void *buffer)
{
    if (lba >= ata_sectors)
        return -1;

    if (ata_prepare_lba(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_READ);
    if (ata_wait_drq() != 0)
        return -1;

    unsigned short *words = (unsigned short *)buffer;
    for (int i = 0; i < 256; i++)
        words[i] = inw(ATA_DATA);

    return 0;
}

static int ata_write(unsigned int lba, const void *buffer)
{
    if (lba >= ata_sectors)
        return -1;

    if (ata_prepare_lba(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (ata_wait_drq() != 0)
        return -1;

    const unsigned short *words = (const unsigned short *)buffer;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, words[i]);

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait_not_busy();
}

static int raw_read(int id, unsigned int lba, void *buffer, unsigned int count)
{
    if (id == DISK_USB) return usb_storage_read(lba, buffer, count);
    if (id == DISK_ATA)
    {
        for (unsigned int i = 0; i < count; i++)
            if (ata_read(lba + i, (unsigned char *)buffer + i * 512)) return -1;
        return 0;
    }
    if (id == DISK_RAM && ram_ready && lba < DISK_SECTORS && count <= DISK_SECTORS - lba)
    { memcpy(buffer, ram_disk[lba], count * 512); return 0; }
    return -1;
}
static unsigned int little32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
int disk_select(const char *name)
{
    DiskSelection next = {DISK_NONE, 0, 0, 0};
    unsigned int physical = 0;
    if (strcmp(name, "usb0") == 0)
    {
        if (usb_storage_probe()) return -1;
        next.id = DISK_USB; physical = usb_storage_sectors();
        next.generation = usb_storage_generation();
    }
    else if (strcmp(name, "ata0") == 0)
    {
        if (ata_identify()) return -1;
        next.id = DISK_ATA; physical = ata_sectors;
    }
    else if (strcmp(name, "ram0") == 0 && ram_ready)
    { next.id = DISK_RAM; physical = DISK_SECTORS; }
    else return -1;
    next.sectors = physical;
    unsigned char sector[512];
    /* Prefer a raw ext2 superblock; otherwise locate the first Linux primary
     * MBR partition. Unsupported filesystems are rejected later, never formatted. */
    if (physical < 4 || raw_read(next.id, 2, sector, 1)) return -1;
    if (sector[56] != 0x53 || sector[57] != 0xEF)
    {
        if (raw_read(next.id, 0, sector, 1)) return -1;
        if (sector[510] == 0x55 && sector[511] == 0xAA)
        {
            int found = 0;
            for (int i = 0; i < 4; i++)
            {
                const unsigned char *entry = sector + 446 + i * 16;
                if (entry[4] != 0x83) continue;
                unsigned int start = little32(entry + 8), count = little32(entry + 12);
                if (!start || !count || start >= physical || count > physical - start) return -1;
                next.offset = start; next.sectors = count; found = 1; break;
            }
            if (!found) return -1;
        }
    }
    if (next.sectors > DISK_SECTORS) next.sectors = DISK_SECTORS;
    selected = next;
    return 0;
}
int disk_init(void) { return disk_select("ata0"); }
int disk_init_from_memory(const void *data, unsigned int size)
{
    if (!data || size != sizeof(ram_disk)) return -1;
    memcpy(ram_disk, data, size); ram_ready = 1;
    selected = (DiskSelection){DISK_RAM, 0, DISK_SECTORS, 0};
    return 0;
}
DiskSelection disk_selection(void) { return selected; }
void disk_restore(DiskSelection selection) { selected = selection; }
void disk_deselect(void) { selected = (DiskSelection){DISK_NONE, 0, 0, 0}; }
const char *disk_name(void)
{
    return selected.id == DISK_USB ? "usb0" : selected.id == DISK_ATA ? "ata0" :
           selected.id == DISK_RAM ? "ram0" : "none";
}
int disk_present(void)
{
    return selected.id == DISK_USB ? usb_storage_present() &&
                                    selected.generation == usb_storage_generation() :
           selected.id == DISK_ATA ? ata_sectors != 0 : selected.id == DISK_RAM && ram_ready;
}
int disk_read_many(unsigned int lba, void *buffer, unsigned int count)
{
    if (!disk_present() || !count || count > 8 || lba >= selected.sectors ||
        count > selected.sectors - lba) return -1;
    return raw_read(selected.id, lba + selected.offset, buffer, count);
}
int disk_read(unsigned int lba, void *buffer) { return disk_read_many(lba, buffer, 1); }
int disk_write(unsigned int lba, const void *buffer)
{
    if (!disk_present() || lba >= selected.sectors) return -1;
    if (selected.id == DISK_USB) return usb_storage_write(lba + selected.offset, buffer, 1);
    if (selected.id == DISK_ATA) return ata_write(lba + selected.offset, buffer);
    memcpy(ram_disk[lba], buffer, 512); return 0;
}
int disk_flush(void)
{
    if (!disk_present()) return -1;
    if (selected.id == DISK_USB) return usb_storage_flush();
    if (selected.id == DISK_ATA)
    {
        if (ata_wait_not_busy()) return -1;
        outb(ATA_COMMAND, ATA_CMD_FLUSH);
        if (ata_wait_not_busy() || (inb(ATA_STATUS) & 0x21)) return -1;
    }
    return 0;
}
void disk_list(void)
{
    if (ram_ready) println(WHITE, tr("ram0 - Live filesystem in RAM"));
    if (!ata_identify()) println(WHITE, tr("ata0 - primary ATA disk"));
    if (!usb_storage_probe()) println(WHITE, tr("usb0 - USB mass storage (UHCI)"));
    else println(WHITE, tr("usb0 - unavailable (requires UHCI and full-speed USB storage)"));
}
void disk_info(void)
{
    if (!disk_present()) { println(RED, tr("no active device")); return; }
    println(WHITE, selected.id == DISK_USB ? tr("Device: USB mass storage (persistent)") :
                   selected.id == DISK_ATA ? tr("Device: primary ATA (persistent)") :
                                            tr("Device: RAM disk (volatile)"));
    print(WHITE, tr("Name: ")); println(WHITE, disk_name());
    print(WHITE, tr("Device sectors (512 bytes): "));
    print_uint(WHITE, selected.id == DISK_USB ? usb_storage_sectors() :
                      selected.id == DISK_ATA ? ata_sectors : DISK_SECTORS);
    println(WHITE, "");
    print(WHITE, tr("Filesystem start LBA: ")); print_uint(WHITE, selected.offset); println(WHITE, "");
    print(WHITE, tr("Driver-accessible sectors: ")); print_uint(WHITE, selected.sectors); println(WHITE, "");
}
