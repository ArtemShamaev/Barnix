#include "lang.h"
#include "disk.h"
#include "barnix.h"
#include "usb_storage.h"
#include "storage_pci.h"

#define DISK_SECTORS 4096

#define ATA_DATA (ata_base + 0)
#define ATA_SECCOUNT (ata_base + 2)
#define ATA_LBA_LOW (ata_base + 3)
#define ATA_LBA_MID (ata_base + 4)
#define ATA_LBA_HIGH (ata_base + 5)
#define ATA_DRIVE (ata_base + 6)
#define ATA_STATUS (ata_base + 7)
#define ATA_COMMAND (ata_base + 7)

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
static uint64_t ata_capacity[4];
static unsigned char ata_lba48[4];
static int ata_unit;
#define ata_sectors ata_capacity[ata_unit]
#define ata_base (ata_unit < 2 ? 0x1F0 : 0x170)
#define ata_slave ((ata_unit & 1) << 4)
static int is_ata(int id) { return id == DISK_ATA || (id >= 4 && id <= 6); }
static int ata_id(int unit) { return unit ? unit + 3 : DISK_ATA; }
static void ata_use(int id) { ata_unit = id == DISK_ATA ? 0 : id - 3; }
static const char *usb_names[USB_STORAGE_MAX] = {"usb0", "usb1", "usb2", "usb3", "usb4", "usb5", "usb6", "usb7", "usb8", "usb9", "usb10", "usb11", "usb12", "usb13", "usb14", "usb15", "usb16", "usb17", "usb18", "usb19", "usb20", "usb21", "usb22", "usb23", "usb24", "usb25", "usb26", "usb27", "usb28", "usb29", "usb30", "usb31", "usb32", "usb33", "usb34", "usb35", "usb36", "usb37", "usb38", "usb39", "usb40", "usb41", "usb42", "usb43", "usb44", "usb45", "usb46", "usb47"};
static int is_usb(int id) { return id == DISK_USB || (id >= 8 && id < 8 + USB_STORAGE_MAX - 1); }
static int usb_id(int unit) { return unit ? 7 + unit : DISK_USB; }
static int usb_unit(int id) { return id == DISK_USB ? 0 : id - 7; }
static int usb_name_unit(const char *name)
{
    for (int i = 0; i < USB_STORAGE_MAX; i++) if (!strcmp(name, usb_names[i])) return i;
    return -1;
}


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

static int ata_select_drive(void)
{
    outb(ATA_DRIVE, 0xE0 | ata_slave);
    io_wait();
    return ata_wait_not_busy();
}

static int ata_identify(void)
{
    ata_sectors = 0;
    if (ata_select_drive() != 0)
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
    ata_lba48[ata_unit] = !!(identity[83] & (1U << 10));
    ata_sectors = identity[60] | ((unsigned int)identity[61] << 16);
    if (ata_lba48[ata_unit]) ata_sectors = (uint64_t)identity[100] | ((uint64_t)identity[101] << 16) |
        ((uint64_t)identity[102] << 32) | ((uint64_t)identity[103] << 48);
    /* This PIO path uses 512-byte logical sectors; AHCI also supports 4Kn. */
    if ((identity[106] & 0xd000) == 0x5000 && (identity[117] != 256 || identity[118])) return -1;
    if (!ata_sectors) return -1;

    return 0;
}

static int ata_prepare_lba(unsigned int lba)
{
    if (!ata_lba48[ata_unit] && lba >= (1U << 28))
        return -1;

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_DRIVE, 0xE0 | ata_slave | (ata_lba48[ata_unit] ? 0 : ((lba >> 24) & 0x0F)));
    if (ata_lba48[ata_unit]) {
        outb(ATA_SECCOUNT, 0); outb(ATA_LBA_LOW, lba >> 24);
        outb(ATA_LBA_MID, 0); outb(ATA_LBA_HIGH, 0);
    }
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

    outb(ATA_COMMAND, ata_lba48[ata_unit] ? 0x24 : ATA_CMD_READ);
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

    outb(ATA_COMMAND, ata_lba48[ata_unit] ? 0x34 : ATA_CMD_WRITE);
    if (ata_wait_drq() != 0)
        return -1;

    const unsigned short *words = (const unsigned short *)buffer;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, words[i]);

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_COMMAND, ata_lba48[ata_unit] ? 0xea : ATA_CMD_FLUSH);
    return ata_wait_not_busy();
}

static int raw_read(int id, unsigned int lba, void *buffer, unsigned int count)
{
    if (id >= DISK_PCI_BASE) return storage_pci_read(id - DISK_PCI_BASE, lba, buffer, count);
    if (is_usb(id)) { if (usb_storage_select(usb_unit(id))) return -1; return usb_storage_read(lba, buffer, count); }
    if (is_ata(id))
    {
        ata_use(id);
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
static int select_device(const char *name, int raw)
{
    DiskSelection next = {DISK_NONE, 0, 0, 0};
    unsigned int physical = 0;
    int usb = usb_name_unit(name);
    if (usb >= 0)
    {
        if (usb_storage_select(usb) || usb_storage_probe()) return -1;
        next.id = usb_id(usb); physical = usb_storage_sectors();
        next.generation = usb_storage_generation();
    }
    else if (strcmp(name, "ata0") == 0 ||
             (!strncmp(name, "disk", 4) && name[4] >= '1' && name[4] <= '4' && !name[5]))
    {
        ata_unit = name[0] == 'a' ? 0 : name[4] - '1';
        if (ata_identify()) return -1;
        next.id = ata_id(ata_unit); physical = ata_sectors > 0xffffffffU ? 0xffffffffU : (unsigned int)ata_sectors;
    }
    else if (strcmp(name, "ram0") == 0 && ram_ready)
    { next.id = DISK_RAM; physical = DISK_SECTORS; }
    else {
        storage_pci_init();
        for (int i = 0; i < storage_pci_count(); i++) if (!strcmp(name, storage_pci_name(i)) && storage_pci_present(i)) {
            uint64_t capacity = storage_pci_capacity(i);
            next.id = DISK_PCI_BASE + i; physical = capacity > 0xffffffffU ? 0xffffffffU : (unsigned int)capacity; break;
        }
        if (next.id == DISK_NONE) return -1;
    }
    next.sectors = physical;
    if (raw) { selected = next; return 0; }
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
    selected = next;
    return 0;
}
static int select_named(const char *name,int raw) {
    int n=strlen(name);
    if(n>=3 && name[n-2]=='p' && name[n-1]>='1' && name[n-1]<='4') {
        char base[32];if(n-2>=32)return -1;memcpy(base,name,n-2);base[n-2]=0;
        if(select_device(base,1))return -1;
        unsigned char mbr[512];if(disk_read(0,mbr)||mbr[510]!=0x55||mbr[511]!=0xaa)return -1;
        unsigned char *entry=mbr+446+(name[n-1]-'1')*16;
        unsigned int start=little32(entry+8),size=little32(entry+12);
        if(!entry[4]||entry[4]==0xee||!start||!size||start>=selected.sectors||size>selected.sectors-start)return -1;
        selected.offset=start;selected.sectors=size;return 0;
    }
    return select_device(name,raw);
}
int disk_select(const char *name) { return select_named(name, 0); }
int disk_select_raw(const char *name) { return select_named(name, 1); }
int disk_init(void) { return disk_select("ata0"); }
int disk_init_from_memory(const void *data, unsigned int size)
{
    if (!data || size != sizeof(ram_disk)) return -1;
    memcpy(ram_disk, data, size); ram_ready = 1;
    selected = (DiskSelection){DISK_RAM, 0, DISK_SECTORS, 0};
    return 0;
}
DiskSelection disk_selection(void) { return selected; }
void disk_restore(DiskSelection selection) { selected = selection; if (is_ata(selected.id)) ata_use(selected.id); }
void disk_deselect(void) { selected = (DiskSelection){DISK_NONE, 0, 0, 0}; }
const char *disk_name(void)
{
    if (selected.id >= DISK_PCI_BASE) return storage_pci_name(selected.id - DISK_PCI_BASE);
    if (is_ata(selected.id)) { ata_use(selected.id); }
    return is_usb(selected.id) ? usb_names[usb_unit(selected.id)] : is_ata(selected.id) ? (const char *[]) {"disk1", "disk2", "disk3", "disk4"}[ata_unit] :
           selected.id == DISK_RAM ? "ram0" : "none";
}
int disk_present(void)
{
    if (selected.id >= DISK_PCI_BASE) return storage_pci_present(selected.id - DISK_PCI_BASE);
    if (is_ata(selected.id)) { ata_use(selected.id); }
    if (is_usb(selected.id) && usb_storage_select(usb_unit(selected.id))) return 0;
    return is_usb(selected.id) ? usb_storage_present() &&
                                    selected.generation == usb_storage_generation() :
           is_ata(selected.id) ? ata_sectors != 0 : selected.id == DISK_RAM && ram_ready;
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
    if (selected.id >= DISK_PCI_BASE) return storage_pci_write(selected.id - DISK_PCI_BASE, lba + selected.offset, buffer, 1);
    if (is_usb(selected.id)) return usb_storage_write(lba + selected.offset, buffer, 1);
    if (is_ata(selected.id)) return ata_write(lba + selected.offset, buffer);
    memcpy(ram_disk[lba], buffer, 512); return 0;
}
int disk_flush(void)
{
    if (!disk_present()) return -1;
    if (selected.id >= DISK_PCI_BASE) return storage_pci_flush(selected.id - DISK_PCI_BASE);
    if (is_usb(selected.id)) return usb_storage_flush();
    if (is_ata(selected.id))
    {
        if (ata_select_drive()) return -1;
        outb(ATA_COMMAND, ata_lba48[ata_unit] ? 0xea : ATA_CMD_FLUSH);
        if (ata_wait_not_busy() || (inb(ATA_STATUS) & 0x21)) return -1;
    }
    return 0;
}
static char device_names[DISK_MAX_DEVICES][24];
static int device_names_count;
int disk_device_count(void)
{
    DiskSelection previous = selected;
    const char *legacy[] = {"ram0", "disk1", "disk2", "disk3", "disk4"};
    device_names_count = 0;
    for (unsigned int i = 0; i < sizeof(legacy) / sizeof(legacy[0]); i++)
        if (!disk_select_raw(legacy[i])) strcpy(device_names[device_names_count++], legacy[i]);
    for (int i = 0; i < USB_STORAGE_MAX && device_names_count < DISK_MAX_DEVICES; i++)
        if (!disk_select_raw(usb_names[i])) strcpy(device_names[device_names_count++], usb_names[i]);
    storage_pci_init();
    for (int i = 0; i < storage_pci_count() && device_names_count < DISK_MAX_DEVICES; i++)
        if (storage_pci_present(i)) strcpy(device_names[device_names_count++], storage_pci_name(i));
    disk_restore(previous); return device_names_count;
}
const char *disk_device_name(int index)
{ return index >= 0 && index < device_names_count ? device_names[index] : "none"; }
const char *disk_type(void)
{
    return selected.id >= DISK_PCI_BASE ? storage_pci_type(selected.id - DISK_PCI_BASE) :
           selected.id == DISK_RAM ? "ram" : is_usb(selected.id) ? "usb" : "disk";
}
uint64_t disk_capacity(void)
{
    if (selected.id >= DISK_PCI_BASE) return storage_pci_capacity(selected.id - DISK_PCI_BASE);
    if (is_usb(selected.id)) { if (usb_storage_select(usb_unit(selected.id))) return 0; return usb_storage_sectors(); }
    if (is_ata(selected.id)) { ata_use(selected.id); return ata_sectors; }
    return selected.id == DISK_RAM ? DISK_SECTORS : 0;
}
void disk_list(void)
{
    DiskSelection previous = selected;
    int count = disk_device_count();
    usb_storage_diagnostics();
    for (int i = 0; i < count; i++) if (!disk_select_raw(disk_device_name(i))) {
        print(WHITE, disk_name());
        if (is_ata(selected.id)) println(WHITE, " - ATA disk");
        else if (selected.id == DISK_RAM) println(WHITE, " - Live filesystem in RAM");
        else if (is_usb(selected.id)) println(WHITE, " - USB mass storage");
        else { print(WHITE, " - "); println(WHITE, disk_type()); }
    }
    disk_restore(previous);
}
void disk_info(void)
{
    if (!disk_present()) { println(RED, tr("no active device")); return; }
    if (selected.id >= DISK_PCI_BASE) { print(WHITE, "Device: "); println(WHITE, disk_type()); }
    else println(WHITE, is_usb(selected.id) ? tr("Device: USB mass storage (persistent)") :
                   is_ata(selected.id) ? tr("Device: ATA disk (persistent)") :
                                            tr("Device: RAM disk (volatile)"));
    print(WHITE, tr("Name: ")); println(WHITE, disk_name());
    print(WHITE, tr("Device sectors (512 bytes): "));
    char capacity_text[21]; disk_u64toa(disk_capacity(), capacity_text); print(WHITE, capacity_text);
    println(WHITE, "");
    print(WHITE, tr("Filesystem start LBA: ")); print_uint(WHITE, selected.offset); println(WHITE, "");
    print(WHITE, tr("Driver-accessible sectors: ")); print_uint(WHITE, selected.sectors); println(WHITE, "");
}
